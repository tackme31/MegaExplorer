#include "FolderNavigationController.h"

#include "app/Logging.h"
#include "NotificationController.h"

#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include <QString>
#include <QVariantMap>

namespace
{

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
{}

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
    mFileOps->rename(static_cast<std::uint64_t>(handle),
                     newName.toStdString(),
                     [this, self = shared_from_this()](Result<void> result) {
                         invokeOnGuiThread(this, [this, result = std::move(result)]() {
                             if (!result.success)
                             {
                                 qCWarning(lcFileOps)
                                     << "rename failed:"
                                     << QString::fromStdString(result.errorMessage)
                                     << "code=" << result.errorCode;
                                 mNotifications->notifyError(
                                     QStringLiteral("rename"),
                                     QString::fromStdString(result.errorMessage));
                                 return;
                             }
                             refreshVisibleListing();
                         });
                     });
}

void FolderNavigationController::moveSelectionToRubbish()
{
    const QVariantList entries = mFileListModel.selectedEntries();
    if (entries.isEmpty())
        return;

    auto batch = std::make_shared<RubbishBatch>();
    batch->remaining = static_cast<int>(entries.size());

    for (const QVariant& entry : entries)
    {
        const quint64 handle = entry.toMap().value(QStringLiteral("handle")).toULongLong();
        mFileOps->moveToRubbish(
            static_cast<std::uint64_t>(handle),
            [this, self = shared_from_this(), batch](Result<void> result) {
                invokeOnGuiThread(this, [this, batch, result = std::move(result)]() {
                    if (result.success)
                    {
                        ++batch->succeeded;
                    }
                    else
                    {
                        ++batch->failed;
                        qCWarning(lcFileOps) << "move to rubbish failed:"
                                             << QString::fromStdString(result.errorMessage)
                                             << "code=" << result.errorCode;
                    }

                    if (--batch->remaining > 0)
                        return;

                    refreshVisibleListing();
                    mNotifications->notifyOperation(QStringLiteral("moveToRubbish"),
                                                    batch->succeeded,
                                                    batch->failed);
                });
            });
    }
}

void FolderNavigationController::reset()
{
    mService->resetToRoot();
    mFileListModel.setEntries({});
    mLastFolderEntries.clear();
    mLastSearchQuery.clear();
    mHasLoadedOnce = false;
    mBreadcrumb.clear();
    emit canGoBackChanged();
    emit breadcrumbChanged();
}
