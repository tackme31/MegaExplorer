#include "FolderNavigationController.h"

#include "app/Logging.h"
#include "ClipboardController.h"
#include "core/MegaErrorCodes.h"
#include "GuiThread.h"
#include "NotificationController.h"

#include <QDebug>
#include <QString>
#include <QVariantMap>

#include <set>

namespace
{

// How long an operation has to stay in flight before the tab shows a spinner.
// Every operation the busy count covers is a server round-trip, so most land
// well inside this and never flash one; the ones that don't are the ones
// worth reporting.
constexpr int kBusyIndicatorDelayMs = 250;

} // namespace

FolderNavigationController::FolderNavigationController(
    std::shared_ptr<FolderNavigationService> navigationService,
    std::shared_ptr<SearchService> searchService,
    std::shared_ptr<FileOperationService> fileOperationService,
    NotificationController* notifications,
    ClipboardController* clipboard,
    QObject* parent)
    : QObject(parent), mService(std::move(navigationService)),
      mSearchService(std::move(searchService)), mFileOps(std::move(fileOperationService)),
      mNotifications(notifications), mClipboard(clipboard),
      mFileListModel(std::make_shared<FileListModel>())
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
    return mFileListModel.get();
}

std::shared_ptr<FileListModel> FolderNavigationController::fileListModelForThumbnails()
{
    return mFileListModel;
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
    mFileListModel->setEntries(std::move(result.value));
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
        mFileListModel->setEntries(mLastFolderEntries);
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
    mFileListModel->setEntries(std::move(result.value));
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
    const QVariantList entries = mFileListModel->selectedEntries();
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
    // A drag started in this tab, so this tab is where the nodes came from --
    // read *now*, because a refresh mid-batch could in principle move it.
    moveHandlesFrom(handles, target, targetIsRoot, currentHandle(), atRoot());
}

void FolderNavigationController::moveHandlesFrom(const QVariantList& handles,
                                                 quint64 target,
                                                 bool targetIsRoot,
                                                 quint64 source,
                                                 bool sourceIsRoot)
{
    if (handles.isEmpty())
        return;

    auto batch = std::make_shared<BulkOperationBatch>();
    batch->remaining = static_cast<int>(handles.size());
    batch->onComplete =
        [this, target, targetIsRoot, source, sourceIsRoot](const BulkOperationBatch& done) {
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

bool FolderNavigationController::canPaste() const
{
    if (!mHasLoadedOnce)
        return false;
    if (!mClipboard->canPasteInto(currentHandle(), atRoot()))
        return false;
    if (!mFileOps->canAddChildren(static_cast<std::uint64_t>(currentHandle()), atRoot()).success)
        return false;
    return mClipboard->isCut() || clipboardCopyAllowedHere().success;
}

Result<void> FolderNavigationController::clipboardCopyAllowedHere() const
{
    for (const ClipboardController::Entry& entry : mClipboard->entries())
    {
        Result<void> allowed = mFileOps->canCopy(static_cast<std::uint64_t>(entry.handle),
                                                 static_cast<std::uint64_t>(currentHandle()),
                                                 atRoot());
        if (!allowed.success)
            return allowed;
    }
    return Result<void>::ok();
}

void FolderNavigationController::paste()
{
    // Ctrl+V is reachable before the first listing has ever loaded, and the two
    // clipboard cases are exactly the ones canPaste() greys out -- nothing to
    // report in any of them.
    if (!mHasLoadedOnce || !mClipboard->hasContent())
        return;
    if (!mClipboard->canPasteInto(currentHandle(), atRoot()))
        return;

    const Result<void> allowed =
        mFileOps->canAddChildren(static_cast<std::uint64_t>(currentHandle()), atRoot());
    if (!allowed.success)
    {
        // The one refusal that does get a toast: unlike the silent cases above,
        // a read-only share or a vanished destination gives the user no way to
        // guess why nothing happened.
        qCWarning(lcFileOps) << "paste rejected:" << QString::fromStdString(allowed.errorMessage)
                             << "code=" << allowed.errorCode;
        mNotifications->notifyError(QStringLiteral("paste"),
                                    QString::fromStdString(allowed.errorMessage));
        return;
    }

    if (mClipboard->isCut())
    {
        QVariantList handles;
        handles.reserve(static_cast<qsizetype>(mClipboard->entries().size()));
        for (const ClipboardController::Entry& entry : mClipboard->entries())
            handles.append(QVariant::fromValue(entry.handle));
        const quint64 source = mClipboard->sourceHandle();
        const bool sourceIsRoot = mClipboard->sourceIsRoot();
        // Emptied as the paste is *issued*, like Explorer: the ghosting has to
        // stop now, and a half-failed batch must not leave a clipboard whose
        // nodes are partly somewhere else. A copy keeps its content, so pasting
        // twice is a legitimate way to get two copies.
        mClipboard->clear();
        moveHandlesFrom(handles, currentHandle(), atRoot(), source, sourceIsRoot);
        return;
    }

    // Copying a folder into its own subtree is refused here as it is on a
    // Ctrl+drop -- MEGA would snapshot-duplicate the whole tree, and no user
    // asks for that on purpose. Toasted rather than silent: canPaste() greys
    // the menu entry, so reaching this means Ctrl+V, where nothing else would
    // explain the silence.
    const Result<void> copyAllowed = clipboardCopyAllowedHere();
    if (!copyAllowed.success)
    {
        qCWarning(lcFileOps) << "paste rejected:"
                             << QString::fromStdString(copyAllowed.errorMessage)
                             << "code=" << copyAllowed.errorCode;
        mNotifications->notifyError(QStringLiteral("paste"),
                                    QString::fromStdString(copyAllowed.errorMessage));
        return;
    }

    // Re-read the destination's names before choosing any: the cached listing
    // could be stale, and a name that collides silently versions over the
    // existing file instead of landing beside it (IMegaClient::copyNode).
    // refreshCurrent touches neither the back-stack nor the current location,
    // so this is also correct while a search is showing.
    const quint64 target = currentHandle();
    const bool targetIsRoot = atRoot();
    beginBusyOperation();
    mService->refreshCurrent(
        mSortOrder,
        [this, self = shared_from_this(), target, targetIsRoot](
            Result<std::vector<FileEntry>> result) {
            invokeOnGuiThread(
                this, [this, target, targetIsRoot, result = std::move(result)]() mutable {
                    endBusyOperation();
                    const std::vector<ClipboardController::Entry>& entries = mClipboard->entries();
                    if (entries.empty())
                        return; // cleared while the destination read was in flight

                    // A failed read is no reason to refuse the paste: the
                    // destination *is* the folder this tab is showing, so the
                    // cached listing of it is the best answer available.
                    std::set<std::string> taken;
                    for (const FileEntry& entry :
                         (result.success ? result.value : mLastFolderEntries))
                        taken.insert(entry.name);
                    startCopyBatch(entries, target, targetIsRoot, std::move(taken));
                });
        });
}

void FolderNavigationController::copyEntriesTo(const QVariantList& entries,
                                               quint64 target,
                                               bool targetIsRoot)
{
    const std::vector<ClipboardController::Entry> copied = ClipboardController::toEntries(entries);
    if (copied.empty())
        return;

    const Result<void> allowed =
        mFileOps->canAddChildren(static_cast<std::uint64_t>(target), targetIsRoot);
    if (!allowed.success)
    {
        qCWarning(lcFileOps) << "drop-copy rejected:"
                             << QString::fromStdString(allowed.errorMessage)
                             << "code=" << allowed.errorCode;
        mNotifications->notifyError(QStringLiteral("copy"),
                                    QString::fromStdString(allowed.errorMessage));
        return;
    }

    beginBusyOperation();
    mService->listChildrenOf(
        static_cast<std::uint64_t>(target),
        targetIsRoot,
        mSortOrder,
        [this, self = shared_from_this(), copied, target, targetIsRoot](
            Result<std::vector<FileEntry>> result) {
            invokeOnGuiThread(
                this, [this, copied, target, targetIsRoot, result = std::move(result)]() mutable {
                    endBusyOperation();
                    // No fallback here, unlike paste(): the destination is
                    // whatever folder the pointer was over, and this tab holds
                    // no listing of it. Copying under names picked against the
                    // wrong folder is exactly what versions over an existing
                    // file, so a failed read has to end the drop.
                    if (!result.success)
                    {
                        qCWarning(lcFileOps) << "drop-copy destination read failed:"
                                             << QString::fromStdString(result.errorMessage);
                        mNotifications->notifyError(QStringLiteral("copy"),
                                                    QString::fromStdString(result.errorMessage));
                        return;
                    }

                    std::set<std::string> taken;
                    for (const FileEntry& entry : result.value)
                        taken.insert(entry.name);
                    startCopyBatch(copied, target, targetIsRoot, std::move(taken));
                });
        });
}

void FolderNavigationController::startCopyBatch(
    const std::vector<ClipboardController::Entry>& entries,
    quint64 target,
    bool targetIsRoot,
    std::set<std::string> taken)
{
    if (entries.empty())
        return;

    auto batch = std::make_shared<BulkOperationBatch>();
    batch->remaining = static_cast<int>(entries.size());
    // Only the destination gained anything, so re-read this tab only when it is
    // the destination -- which is always true for a paste and usually false for
    // a Ctrl+drop. Every other tab showing it is reached through nodesCopied.
    batch->refresh = [this, target, targetIsRoot] {
        refreshIfShowing(target, targetIsRoot);
    };
    batch->onComplete = [this, target, targetIsRoot](const BulkOperationBatch& done) {
        if (done.succeeded > 0)
            emit nodesCopied(target, targetIsRoot);
    };

    for (const ClipboardController::Entry& entry : entries)
    {
        const std::string sourceName = entry.name.toStdString();
        const std::string chosen =
            FileOperationService::uniqueCopyName(sourceName, entry.isFolder, taken);
        // Claimed right away: MEGA allows duplicate siblings, so two clipboard
        // entries can share a name and must not be handed the same new one.
        taken.insert(chosen);

        beginBusyOperation();
        mFileOps->copy(static_cast<std::uint64_t>(entry.handle),
                       static_cast<std::uint64_t>(target),
                       targetIsRoot,
                       chosen == sourceName ? std::string() : chosen,
                       [this, self = shared_from_this(), batch](Result<void> result) {
                           invokeOnGuiThread(this, [this, batch, result = std::move(result)]() {
                               endBusyOperation();
                               accountForBulkOutcome(batch, result, "copy");
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

bool FolderNavigationController::canCopyEntriesOn(const QVariantList& entries,
                                                  quint64 target,
                                                  bool targetIsRoot) const
{
    if (entries.isEmpty())
        return false;

    if (!mFileOps->canAddChildren(static_cast<std::uint64_t>(target), targetIsRoot).success)
        return false;

    for (const QVariant& entry : entries)
    {
        const quint64 handle = entry.toMap().value(QStringLiteral("handle")).toULongLong();
        if (!mFileOps
                 ->canCopy(static_cast<std::uint64_t>(handle),
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

    if (batch->refresh)
        batch->refresh();
    else
        refreshVisibleListing();
    mNotifications->notifyOperation(QString::fromLatin1(context), batch->succeeded, batch->failed);
    if (batch->onComplete)
        batch->onComplete(*batch);
}

void FolderNavigationController::reset()
{
    mService->resetToRoot();
    mFileListModel->setEntries({});
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
