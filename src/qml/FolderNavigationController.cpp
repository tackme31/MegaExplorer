#include "FolderNavigationController.h"

#include "app/Logging.h"
#include "core/MegaErrorCodes.h"
#include "NotificationController.h"

#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include <QString>
#include <QVariantMap>

namespace
{

// How long an operation has to stay in flight before the tab shows a spinner.
// Every operation the busy count covers is a server round-trip, so most land
// well inside this and never flash one; the ones that don't are the ones
// worth reporting.
constexpr int kBusyIndicatorDelayMs = 250;

// Service callbacks may fire on an SDK-internal background thread (see
// IMegaClient.h), so touching the QML-facing model/property from there must
// go through a queued invoke onto the GUI thread. Same idiom as main.cpp's
// own invokeOnGuiThread; duplicated here rather than shared since it's a
// trivial 3-line, stateless helper.
//
// target is `this` at every call site below (Phase 9), not qApp: QObject's
// destructor removes any posted events still queued for it, so a controller
// destroyed (tab closed) after this call but before the GUI thread processes
// the queued fn simply drops it instead of running fn against a dangling
// `this`. Every outer lambda passed to the service methods below also
// captures a shared_from_this() copy, covering the earlier window (between
// the SDK background thread invoking that outer lambda and this function
// actually posting the event) during which the controller must stay alive
// for `this`/`target` itself to be valid.
void invokeOnGuiThread(QObject* target, std::function<void()> fn)
{
    QMetaObject::invokeMethod(target, std::move(fn), Qt::QueuedConnection);
}

} // namespace

FolderNavigationController::FolderNavigationController(
    std::shared_ptr<FolderNavigationService> navigationService,
    std::shared_ptr<SearchService> searchService,
    std::shared_ptr<FileOperationService> fileOperationService,
    NotificationController* notifications,
    QObject* parent)
    : QObject(parent), mService(std::move(navigationService)),
      mSearchService(std::move(searchService)), mFileOps(std::move(fileOperationService)),
      mNotifications(notifications)
{
    mBusyDelayTimer.setSingleShot(true);
    mBusyDelayTimer.setInterval(kBusyIndicatorDelayMs);
    connect(&mBusyDelayTimer, &QTimer::timeout, this, [this]() {
        // The timer is stopped by endBusyOperation, so this normally can't
        // fire with nothing left in flight -- guarded anyway rather than
        // relying on that ordering.
        if (mBusyCount == 0 || mBusyVisible)
            return;
        mBusyVisible = true;
        emit busyChanged();
    });
}

QObject* FolderNavigationController::fileListModel()
{
    return &mFileListModel;
}

FileListModel* FolderNavigationController::fileListModelForThumbnails()
{
    return &mFileListModel;
}

bool FolderNavigationController::canGoBack() const
{
    return mService->canGoBack();
}

bool FolderNavigationController::busy() const
{
    return mBusyVisible;
}

void FolderNavigationController::beginBusyOperation()
{
    if (++mBusyCount == 1)
        mBusyDelayTimer.start();
}

void FolderNavigationController::endBusyOperation()
{
    // reset() zeroes the count with operations still in flight, so their
    // callbacks arrive here with nothing left to subtract. Clamping rather
    // than letting the count go negative, which would stop a later
    // beginBusyOperation from ever reaching 1 again.
    if (mBusyCount == 0)
        return;

    if (--mBusyCount > 0)
        return;

    mBusyDelayTimer.stop();
    if (!mBusyVisible)
        return;
    mBusyVisible = false;
    emit busyChanged();
}

QVariantList FolderNavigationController::breadcrumb() const
{
    return mBreadcrumb;
}

QString FolderNavigationController::currentFolderName() const
{
    if (mBreadcrumb.isEmpty())
        return QString();
    return mBreadcrumb.last().toMap().value(QStringLiteral("name")).toString();
}

bool FolderNavigationController::canGoUp() const
{
    return mBreadcrumb.size() >= 2;
}

bool FolderNavigationController::atRoot() const
{
    if (mBreadcrumb.isEmpty())
        return true;
    return mBreadcrumb.last().toMap().value(QStringLiteral("isRoot")).toBool();
}

quint64 FolderNavigationController::currentHandle() const
{
    if (mBreadcrumb.isEmpty())
        return 0;
    return mBreadcrumb.last().toMap().value(QStringLiteral("handle")).toULongLong();
}

void FolderNavigationController::loadRoot()
{
    mService->openRoot(mSortOrder,
                       [this, self = shared_from_this()](Result<std::vector<FileEntry>> result) {
                           invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
                               applyResult(std::move(result));
                           });
                       });
}

void FolderNavigationController::openFolder(quint64 handle)
{
    mService->openFolder(static_cast<std::uint64_t>(handle),
                         mSortOrder,
                         [this, self = shared_from_this()](Result<std::vector<FileEntry>> result) {
                             invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
                                 applyResult(std::move(result));
                             });
                         });
}

void FolderNavigationController::goBack()
{
    mService->goBack(mSortOrder,
                     [this, self = shared_from_this()](Result<std::vector<FileEntry>> result) {
                         invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
                             applyResult(std::move(result));
                         });
                     });
}

void FolderNavigationController::goUp()
{
    if (!canGoUp())
        return;
    const QVariantMap parent = mBreadcrumb.at(mBreadcrumb.size() - 2).toMap();
    navigateTo(parent.value(QStringLiteral("handle")).toULongLong(),
               parent.value(QStringLiteral("isRoot")).toBool());
}

void FolderNavigationController::navigateTo(quint64 handle, bool isRoot)
{
    mService->navigateTo(static_cast<std::uint64_t>(handle),
                         isRoot,
                         mSortOrder,
                         [this, self = shared_from_this()](Result<std::vector<FileEntry>> result) {
                             invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
                                 applyResult(std::move(result));
                             });
                         });
}

void FolderNavigationController::applyResult(Result<std::vector<FileEntry>> result)
{
    if (!result.success)
    {
        qCWarning(lcNavigation) << "folder navigation failed:"
                                << QString::fromStdString(result.errorMessage)
                                << "code=" << result.errorCode;
        mNotifications->notifyError(QStringLiteral("navigation"),
                                    QString::fromStdString(result.errorMessage));
        return;
    }
    mHasLoadedOnce = true;
    mLastFolderEntries = result.value;
    mFileListModel.setEntries(std::move(result.value));
    emit canGoBackChanged();
    refreshBreadcrumb();
}

void FolderNavigationController::refreshBreadcrumb()
{
    mService->resolveCurrentPath(
        [this, self = shared_from_this()](Result<std::vector<PathSegment>> result) {
            invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
                if (!result.success)
                {
                    qCWarning(lcNavigation) << "breadcrumb path resolution failed:"
                                            << QString::fromStdString(result.errorMessage)
                                            << "code=" << result.errorCode;
                    return;
                }

                QVariantList breadcrumb;
                breadcrumb.reserve(static_cast<qsizetype>(result.value.size()));
                for (const PathSegment& segment : result.value)
                {
                    QVariantMap entry;
                    entry.insert(QStringLiteral("name"), QString::fromStdString(segment.name));
                    entry.insert(QStringLiteral("handle"), static_cast<qulonglong>(segment.handle));
                    entry.insert(QStringLiteral("isRoot"), segment.isRoot);
                    breadcrumb.append(entry);
                }

                // A QVariantList model has no diffing on the QML side, so a
                // Repeater over it rebuilds every delegate on each emit --
                // skip the (re-)assignment entirely when the resolved path is
                // unchanged (e.g. refreshCurrentFolder() re-running this after a
                // sort-order change while staying in the same folder).
                if (breadcrumb == mBreadcrumb)
                    return;
                mBreadcrumb = std::move(breadcrumb);
                emit breadcrumbChanged();
            });
        });
}

void FolderNavigationController::search(QString query)
{
    mLastSearchQuery = query.toStdString();

    if (query.isEmpty())
    {
        mFileListModel.setEntries(mLastFolderEntries);
        return;
    }

    mSearchService->search(
        mLastSearchQuery,
        mSortOrder,
        [this, self = shared_from_this()](Result<std::vector<FileEntry>> result) {
            invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
                applySearchResult(std::move(result));
            });
        });
}

void FolderNavigationController::applySearchResult(Result<std::vector<FileEntry>> result)
{
    if (!result.success)
    {
        qCWarning(lcSearch) << "search failed:" << QString::fromStdString(result.errorMessage)
                            << "code=" << result.errorCode;
        mNotifications->notifyError(QStringLiteral("search"),
                                    QString::fromStdString(result.errorMessage));
        return;
    }
    mFileListModel.setEntries(std::move(result.value));
}

void FolderNavigationController::refreshCurrentFolder()
{
    mService->refreshCurrent(
        mSortOrder, [this, self = shared_from_this()](Result<std::vector<FileEntry>> result) {
            invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
                applyResult(std::move(result));
            });
        });
}

void FolderNavigationController::setSortOrder(int column, bool ascending)
{
    switch (column)
    {
        case 1:
            mSortOrder.key = SortKey::ModificationTime;
            break;
        case 2:
            mSortOrder.key = SortKey::Size;
            break;
        case 0:
        default:
            mSortOrder.key = SortKey::Name;
            break;
    }
    mSortOrder.ascending = ascending;

    // Called once at startup with the Settings-restored value, before
    // loadRoot() has ever run (see mHasLoadedOnce's declaration) -- just
    // record the order in that case, don't fetch yet.
    if (!mHasLoadedOnce)
        return;

    refreshVisibleListing();
}

void FolderNavigationController::refreshVisibleListing()
{
    if (!mLastSearchQuery.empty())
    {
        // Re-run the visible search, and separately refresh the cached folder
        // listing (not the visible model) so that clearing the search
        // afterwards doesn't show stale contents/ordering.
        mSearchService->search(
            mLastSearchQuery,
            mSortOrder,
            [this, self = shared_from_this()](Result<std::vector<FileEntry>> result) {
                invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
                    applySearchResult(std::move(result));
                });
            });
        mService->refreshCurrent(
            mSortOrder, [this, self = shared_from_this()](Result<std::vector<FileEntry>> result) {
                invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
                    if (result.success)
                        mLastFolderEntries = std::move(result.value);
                });
            });
        return;
    }

    refreshCurrentFolder();
}

void FolderNavigationController::renameEntry(quint64 handle, const QString& newName)
{
    beginBusyOperation();
    mFileOps->rename(
        static_cast<std::uint64_t>(handle),
        newName.toStdString(),
        [this, self = shared_from_this()](Result<void> result) {
            invokeOnGuiThread(this, [this, result = std::move(result)]() {
                endBusyOperation();
                if (!result.success)
                {
                    qCWarning(lcFileOps)
                        << "rename failed:" << QString::fromStdString(result.errorMessage)
                        << "code=" << result.errorCode;
                    mNotifications->notifyError(QStringLiteral("rename"),
                                                QString::fromStdString(result.errorMessage));
                    return;
                }
                refreshVisibleListing();
            });
        });
}

void FolderNavigationController::createFolder(const QString& name)
{
    beginBusyOperation();
    mFileOps->createFolder(
        static_cast<std::uint64_t>(currentHandle()),
        atRoot(),
        name.toStdString(),
        [this, self = shared_from_this()](Result<void> result) {
            invokeOnGuiThread(this, [this, result = std::move(result)]() {
                // Above the four-way branching below on purpose: each of those
                // outcomes returns, so anything lower would be four chances to
                // leak the count.
                endBusyOperation();
                if (result.success)
                {
                    refreshVisibleListing();
                    mNotifications->notifyOperation(QStringLiteral("createFolder"), 1, 0);
                    emit folderCreated();
                    return;
                }

                // The two the user can fix in the dialog they're already
                // looking at: no toast, just tell the dialog which it was.
                if (result.errorCode == MegaErrorCode::kEExist)
                {
                    emit folderCreationFailed(QStringLiteral("exists"));
                    return;
                }
                if (result.errorCode == MegaErrorCode::kEArgs)
                {
                    emit folderCreationFailed(QStringLiteral("invalidName"));
                    return;
                }

                qCWarning(lcFileOps)
                    << "create folder failed:" << QString::fromStdString(result.errorMessage)
                    << "code=" << result.errorCode;
                mNotifications->notifyError(QStringLiteral("createFolder"),
                                            QString::fromStdString(result.errorMessage));
                emit folderCreationFailed(QStringLiteral("other"));
            });
        });
}

void FolderNavigationController::moveSelectionToRubbish()
{
    const QVariantList entries = mFileListModel.selectedEntries();
    if (entries.isEmpty())
        return;

    auto batch = std::make_shared<BulkOperationBatch>();
    batch->remaining = static_cast<int>(entries.size());

    for (const QVariant& entry : entries)
    {
        const quint64 handle = entry.toMap().value(QStringLiteral("handle")).toULongLong();
        beginBusyOperation();
        mFileOps->moveToRubbish(static_cast<std::uint64_t>(handle),
                                [this, self = shared_from_this(), batch](Result<void> result) {
                                    invokeOnGuiThread(
                                        this, [this, batch, result = std::move(result)]() {
                                            endBusyOperation();
                                            accountForBulkOutcome(batch, result, "moveToRubbish");
                                        });
                                });
    }
}

void FolderNavigationController::moveHandlesTo(const QVariantList& handles,
                                               quint64 target,
                                               bool targetIsRoot)
{
    if (handles.isEmpty())
        return;

    auto batch = std::make_shared<BulkOperationBatch>();
    batch->remaining = static_cast<int>(handles.size());
    // Where this tab is standing *now*, i.e. where the dragged nodes came
    // from -- captured up front because a refresh mid-batch could in
    // principle move it.
    batch->onComplete =
        [this, target, targetIsRoot, source = currentHandle(), sourceIsRoot = atRoot()](
            const BulkOperationBatch& done) {
            if (done.succeeded > 0)
                emit nodesMoved(target, targetIsRoot, source, sourceIsRoot);
        };

    for (const QVariant& handle : handles)
    {
        beginBusyOperation();
        mFileOps->move(static_cast<std::uint64_t>(handle.toULongLong()),
                       static_cast<std::uint64_t>(target),
                       targetIsRoot,
                       [this, self = shared_from_this(), batch](Result<void> result) {
                           invokeOnGuiThread(this, [this, batch, result = std::move(result)]() {
                               endBusyOperation();
                               accountForBulkOutcome(batch, result, "move");
                           });
                       });
    }
}

bool FolderNavigationController::canDropHandlesOn(const QVariantList& handles,
                                                  quint64 target,
                                                  bool targetIsRoot) const
{
    if (handles.isEmpty())
        return false;

    for (const QVariant& handle : handles)
    {
        if (!mFileOps
                 ->canMove(static_cast<std::uint64_t>(handle.toULongLong()),
                           static_cast<std::uint64_t>(target),
                           targetIsRoot)
                 .success)
            return false;
    }
    return true;
}

void FolderNavigationController::refreshListingIfLoaded()
{
    if (!mHasLoadedOnce)
        return;
    refreshVisibleListing();
}

void FolderNavigationController::refresh()
{
    if (!mHasLoadedOnce)
        return;

    beginBusyOperation();
    mService->syncWithServer([this, self = shared_from_this()](Result<void> result) {
        invokeOnGuiThread(this, [this, result = std::move(result)]() {
            endBusyOperation();
            if (!result.success)
            {
                qCWarning(lcNavigation)
                    << "server sync failed:" << QString::fromStdString(result.errorMessage)
                    << "code=" << result.errorCode;
                mNotifications->notifyError(QStringLiteral("refresh"),
                                            QString::fromStdString(result.errorMessage));
            }
            // Re-read either way. The user asked for the folder to be
            // refreshed; failing to reach the API is worth a toast, but it is
            // no reason to withhold what the SDK already has.
            refreshListingIfLoaded();
        });
    });
}

void FolderNavigationController::refreshIfShowing(quint64 handle, bool isRoot)
{
    if (atRoot() != isRoot)
        return;
    if (!isRoot && currentHandle() != handle)
        return;
    refreshListingIfLoaded();
}

void FolderNavigationController::accountForBulkOutcome(
    const std::shared_ptr<BulkOperationBatch>& batch,
    const Result<void>& result,
    const char* context)
{
    if (result.success)
    {
        ++batch->succeeded;
    }
    else
    {
        ++batch->failed;
        qCWarning(lcFileOps) << context << "failed:" << QString::fromStdString(result.errorMessage)
                             << "code=" << result.errorCode;
    }

    if (--batch->remaining > 0)
        return;

    refreshVisibleListing();
    mNotifications->notifyOperation(QString::fromLatin1(context), batch->succeeded, batch->failed);
    if (batch->onComplete)
        batch->onComplete(*batch);
}

void FolderNavigationController::reset()
{
    mService->resetToRoot();
    mFileListModel.setEntries({});
    mLastFolderEntries.clear();
    mLastSearchQuery.clear();
    mHasLoadedOnce = false;
    mBreadcrumb.clear();
    // Abandons the count rather than waiting the in-flight operations out:
    // this is a logout, their callbacks will find nothing left to refresh, and
    // without this a spinner would keep turning on a signed-out window.
    mBusyCount = 0;
    mBusyDelayTimer.stop();
    const bool wasBusy = mBusyVisible;
    mBusyVisible = false;
    emit canGoBackChanged();
    emit breadcrumbChanged();
    if (wasBusy)
        emit busyChanged();
}
