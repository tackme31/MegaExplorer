#include "FileMutationController.h"

#include "app/Logging.h"
#include "ClipboardController.h"
#include "core/MegaErrorCodes.h"
#include "core/SortOrder.h"
#include "FolderNavigationController.h"
#include "GuiThread.h"
#include "NotificationController.h"

#include <QDebug>
#include <QString>
#include <QVariantMap>

namespace
{
// Both destination reads below pull only child *names* out of the listing, so the
// order is inert and fixed here rather than threaded through from the navigation
// half's current sort.
constexpr SortOrder kNameOrder{SortKey::Name, true};
} // namespace

FileMutationController::FileMutationController(
    std::shared_ptr<FolderNavigationController> navigation,
    std::shared_ptr<FolderNavigationService> navigationService,
    std::shared_ptr<FileOperationService> fileOperationService,
    std::shared_ptr<BusyState> busy,
    NotificationController* notifications,
    ClipboardController* clipboard,
    QObject* parent)
    : QObject(parent), mNavigation(std::move(navigation)), mService(std::move(navigationService)),
      mFileOps(std::move(fileOperationService)), mNotifications(notifications),
      mClipboard(clipboard), mBusy(busy),
      // From the parameters, not the members: this must not depend on where
      // those sit in the declaration order.
      mBulk(*busy, *notifications, [this]() {
          mNavigation->refreshVisibleListing();
      })
{}

void FileMutationController::renameEntry(quint64 handle, const QString& newName)
{
    mBusy->begin();
    mFileOps->rename(
        static_cast<std::uint64_t>(handle),
        newName.toStdString(),
        [this, self = shared_from_this()](Result<void> result) {
            invokeOnGuiThread(this, [this, result = std::move(result)]() {
                mBusy->end();
                if (!result.success)
                {
                    // A name the user can retype isn't an operation failure.
                    if (result.errorCode == MegaErrorCode::kEArgs)
                    {
                        mNotifications->notifyError(QStringLiteral("renameInvalidName"));
                        return;
                    }
                    qCWarning(lcFileOps)
                        << "rename failed:" << QString::fromStdString(result.errorMessage)
                        << "code=" << result.errorCode;
                    mNotifications->notifyError(QStringLiteral("rename"),
                                                result.errorCode,
                                                QString::fromStdString(result.errorMessage));
                    return;
                }
                mNavigation->refreshVisibleListing();
            });
        });
}

void FileMutationController::setEntryFavourite(quint64 handle, bool favourite)
{
    mBusy->begin();
    mFileOps->setFavourite(
        static_cast<std::uint64_t>(handle),
        favourite,
        [this, self = shared_from_this(), handle, favourite](Result<void> result) {
            invokeOnGuiThread(this, [this, handle, favourite, result = std::move(result)]() {
                mBusy->end();
                if (!result.success)
                {
                    qCWarning(lcFileOps)
                        << "setFavourite failed:" << QString::fromStdString(result.errorMessage)
                        << "code=" << result.errorCode;
                    mNotifications->notifyError(favourite ? QStringLiteral("addFavourite")
                                                          : QStringLiteral("removeFavourite"),
                                                result.errorCode,
                                                QString::fromStdString(result.errorMessage));
                    return;
                }
                mNavigation->applyFavouriteChange(handle, favourite);
            });
        });
}

void FileMutationController::createFolder(const QString& name)
{
    mBusy->begin();
    mFileOps->createFolder(
        static_cast<std::uint64_t>(mNavigation->currentHandle()),
        mNavigation->atRoot(),
        name.toStdString(),
        [this, self = shared_from_this()](Result<void> result) {
            invokeOnGuiThread(this, [this, result = std::move(result)]() {
                // Above the four-way branching below on purpose: each of those
                // outcomes returns, so anything lower would be four chances to
                // leak the count.
                mBusy->end();
                if (result.success)
                {
                    mNavigation->refreshVisibleListing();
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
                                            result.errorCode,
                                            QString::fromStdString(result.errorMessage));
                emit folderCreationFailed(QStringLiteral("other"));
            });
        });
}

void FileMutationController::moveHandlesToRubbish(const QVariantList& handles)
{
    if (handles.isEmpty())
        return;

    auto batch = mBulk.start("moveToRubbish", static_cast<int>(handles.size()));

    for (const QVariant& handle : handles)
    {
        mFileOps->moveToRubbish(static_cast<std::uint64_t>(handle.toULongLong()),
                                [this, self = shared_from_this(), batch](Result<void> result) {
                                    invokeOnGuiThread(this, [batch, result = std::move(result)]() {
                                        batch->settle(result);
                                    });
                                });
    }
}

void FileMutationController::moveHandlesTo(const QVariantList& handles,
                                           quint64 target,
                                           bool targetIsRoot)
{
    // A drag started in this tab, so this tab is where the nodes came from --
    // read *now*, because a refresh mid-batch could in principle move it.
    moveHandlesFrom(
        handles, target, targetIsRoot, mNavigation->currentHandle(), mNavigation->atRoot());
}

void FileMutationController::moveHandlesFrom(const QVariantList& handles,
                                             quint64 target,
                                             bool targetIsRoot,
                                             quint64 source,
                                             bool sourceIsRoot)
{
    if (handles.isEmpty())
        return;

    auto batch =
        mBulk.start("move",
                    static_cast<int>(handles.size()),
                    {},
                    [this, target, targetIsRoot, source, sourceIsRoot](int succeeded, int) {
                        if (succeeded > 0)
                            emit nodesMoved(target, targetIsRoot, source, sourceIsRoot);
                    });

    for (const QVariant& handle : handles)
    {
        mFileOps->move(static_cast<std::uint64_t>(handle.toULongLong()),
                       static_cast<std::uint64_t>(target),
                       targetIsRoot,
                       [this, self = shared_from_this(), batch](Result<void> result) {
                           invokeOnGuiThread(this, [batch, result = std::move(result)]() {
                               batch->settle(result);
                           });
                       });
    }
}

bool FileMutationController::canPaste() const
{
    if (!mNavigation->isLoaded())
        return false;
    const quint64 here = mNavigation->currentHandle();
    const bool hereIsRoot = mNavigation->atRoot();
    if (!mClipboard->canPasteInto(here, hereIsRoot))
        return false;
    if (!mFileOps->canAddChildren(static_cast<std::uint64_t>(here), hereIsRoot).success)
        return false;
    return mClipboard->isCut() || clipboardCopyAllowedHere().success;
}

Result<void> FileMutationController::clipboardCopyAllowedHere() const
{
    for (const NodeRef& entry : mClipboard->entries())
    {
        Result<void> allowed =
            mFileOps->canCopy(entry.handle,
                              static_cast<std::uint64_t>(mNavigation->currentHandle()),
                              mNavigation->atRoot());
        if (!allowed.success)
            return allowed;
    }
    return Result<void>::ok();
}

void FileMutationController::paste()
{
    // Ctrl+V is reachable before the first listing has loaded, and canPaste() greys
    // out both clipboard cases -- nothing to report in any of them.
    if (!mNavigation->isLoaded() || !mClipboard->hasContent())
        return;

    const quint64 target = mNavigation->currentHandle();
    const bool targetIsRoot = mNavigation->atRoot();
    if (!mClipboard->canPasteInto(target, targetIsRoot))
        return;

    const Result<void> allowed =
        mFileOps->canAddChildren(static_cast<std::uint64_t>(target), targetIsRoot);
    if (!allowed.success)
    {
        // The one refusal that does get a toast: unlike the silent cases above,
        // a read-only share or a vanished destination gives the user no way to
        // guess why nothing happened.
        qCWarning(lcFileOps) << "paste rejected:" << QString::fromStdString(allowed.errorMessage)
                             << "code=" << allowed.errorCode;
        mNotifications->notifyError(QStringLiteral("paste"),
                                    allowed.errorCode,
                                    QString::fromStdString(allowed.errorMessage));
        return;
    }

    if (mClipboard->isCut())
    {
        QVariantList handles;
        handles.reserve(static_cast<qsizetype>(mClipboard->entries().size()));
        for (const NodeRef& entry : mClipboard->entries())
            handles.append(QVariant::fromValue(static_cast<quint64>(entry.handle)));
        const quint64 source = mClipboard->sourceHandle();
        const bool sourceIsRoot = mClipboard->sourceIsRoot();
        // Emptied as the paste is *issued*, like Explorer: the ghosting has to
        // stop now, and a half-failed batch must not leave a clipboard whose
        // nodes are partly somewhere else. A copy keeps its content, so pasting
        // twice is a legitimate way to get two copies.
        mClipboard->clear();
        moveHandlesFrom(handles, target, targetIsRoot, source, sourceIsRoot);
        return;
    }

    // Toasted rather than silent: canPaste() greys the menu entry, so reaching this
    // means Ctrl+V, where nothing else would explain the silence.
    const Result<void> copyAllowed = clipboardCopyAllowedHere();
    if (!copyAllowed.success)
    {
        qCWarning(lcFileOps) << "paste rejected:"
                             << QString::fromStdString(copyAllowed.errorMessage)
                             << "code=" << copyAllowed.errorCode;
        mNotifications->notifyError(QStringLiteral("paste"),
                                    copyAllowed.errorCode,
                                    QString::fromStdString(copyAllowed.errorMessage));
        return;
    }

    // Re-read the destination's names first: the cached listing could be stale, and a
    // colliding name silently versions over the existing file instead of landing
    // beside it (IMegaClient::copyNode). listChildrenOf rather than refreshCurrent
    // because it reads the folder the paste writes to by construction, not whichever
    // the service considers current -- which also keeps it right during a search.
    mBusy->begin();
    mService->listChildrenOf(
        static_cast<std::uint64_t>(target),
        targetIsRoot,
        kNameOrder,
        [this, self = shared_from_this(), target, targetIsRoot](
            Result<std::vector<FileEntry>> result) {
            invokeOnGuiThread(this,
                              [this, target, targetIsRoot, result = std::move(result)]() mutable {
                                  mBusy->end();
                                  const std::vector<NodeRef>& entries = mClipboard->entries();
                                  if (entries.empty())
                                      return; // cleared while the destination read was in flight

                                  // A failed read is no reason to refuse: the destination *is*
                                  // this tab's folder, so its cached listing is the best answer.
                                  std::set<std::string> taken;
                                  if (result.success)
                                  {
                                      for (const FileEntry& entry : result.value())
                                          taken.insert(entry.name);
                                  }
                                  else
                                  {
                                      taken = mNavigation->cachedChildNames();
                                  }
                                  startCopyBatch(entries, target, targetIsRoot, std::move(taken));
                              });
        });
}

void FileMutationController::copyEntriesTo(const QVariantList& entries,
                                           quint64 target,
                                           bool targetIsRoot)
{
    const std::vector<NodeRef> copied = ClipboardController::toNodeRefs(entries);
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
                                    allowed.errorCode,
                                    QString::fromStdString(allowed.errorMessage));
        return;
    }

    mBusy->begin();
    mService->listChildrenOf(
        static_cast<std::uint64_t>(target),
        targetIsRoot,
        kNameOrder,
        [this, self = shared_from_this(), copied, target, targetIsRoot](
            Result<std::vector<FileEntry>> result) {
            invokeOnGuiThread(
                this, [this, copied, target, targetIsRoot, result = std::move(result)]() mutable {
                    mBusy->end();
                    // No fallback here, unlike paste(): the destination is
                    // whatever folder the pointer was over, and this tab holds
                    // no listing of it. Copying under names picked against the
                    // wrong folder is exactly what versions over an existing
                    // file, so a failed read has to end the drop.
                    if (!result.success)
                    {
                        qCWarning(lcFileOps) << "drop-copy destination read failed:"
                                             << QString::fromStdString(result.errorMessage)
                                             << "code=" << result.errorCode;
                        mNotifications->notifyError(QStringLiteral("copy"),
                                                    result.errorCode,
                                                    QString::fromStdString(result.errorMessage));
                        return;
                    }

                    std::set<std::string> taken;
                    for (const FileEntry& entry : result.value())
                        taken.insert(entry.name);
                    startCopyBatch(copied, target, targetIsRoot, std::move(taken));
                });
        });
}

void FileMutationController::startCopyBatch(const std::vector<NodeRef>& entries,
                                            quint64 target,
                                            bool targetIsRoot,
                                            std::set<std::string> taken)
{
    if (entries.empty())
        return;

    // Only the destination gained anything, so re-read this tab only when it is
    // the destination -- which is always true for a paste and usually false for
    // a Ctrl+drop. Every other tab showing it is reached through nodesCopied.
    auto batch = mBulk.start(
        "copy",
        static_cast<int>(entries.size()),
        [this, target, targetIsRoot]() {
            mNavigation->refreshIfShowing(target, targetIsRoot);
        },
        [this, target, targetIsRoot](int succeeded, int) {
            if (succeeded > 0)
                emit nodesCopied(target, targetIsRoot);
        });

    for (const NodeRef& entry : entries)
    {
        const std::string& sourceName = entry.name;
        const std::string chosen =
            FileOperationService::uniqueCopyName(sourceName, entry.isFolder, taken);
        // Claimed right away: MEGA allows duplicate siblings, so two clipboard
        // entries can share a name and must not be handed the same new one.
        taken.insert(chosen);

        mFileOps->copy(entry.handle,
                       static_cast<std::uint64_t>(target),
                       targetIsRoot,
                       chosen == sourceName ? std::string() : chosen,
                       [this, self = shared_from_this(), batch](Result<void> result) {
                           invokeOnGuiThread(this, [batch, result = std::move(result)]() {
                               batch->settle(result);
                           });
                       });
    }
}

bool FileMutationController::canDropHandlesOn(const QVariantList& handles,
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

bool FileMutationController::canCopyEntriesOn(const QVariantList& entries,
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
