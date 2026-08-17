#include "FileMutationController.h"

#include "app/Logging.h"
#include "ClipboardController.h"
#include "core/MegaErrorCodes.h"
#include "core/SortOrder.h"
#include "core/ViewKind.h"
#include "FolderNavigationController.h"
#include "GuiThread.h"
#include "NotificationController.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QLocale>
#include <QString>
#include <QVariantMap>

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

namespace
{
// Both destination reads below pull only child names and handles out of the
// listing, so the order is inert and fixed here rather than threaded through
// from the navigation half's current sort.
constexpr SortOrder kNameOrder{SortKey::Name, true};

// Empty for zero, so the dialog can drop the parenthetical rather than print
// "(0 B)". Traditional (1024-based) units for the same reason AccountController
// uses them: that is what MEGA itself quotes.
QString sizeText(std::uint64_t bytes)
{
    if (bytes == 0)
        return QString();
    return QLocale::system().formattedDataSize(static_cast<qint64>(bytes),
                                               1,
                                               QLocale::DataSizeTraditionalFormat);
}
} // namespace

FileMutationController::DestinationSnapshot
FileMutationController::DestinationSnapshot::of(const std::vector<FileEntry>& children)
{
    DestinationSnapshot snapshot;
    for (const FileEntry& child : children)
    {
        snapshot.taken.insert(child.name);
        if (child.isFolder)
            snapshot.folders.insert(child.name);
        else
            snapshot.files.insert(child.name);
        snapshot.handles.insert(child.handle);
    }
    return snapshot;
} // namespace

bool FileMutationController::DestinationSnapshot::collidesWith(const NodeRef& entry) const
{
    // Its own node is what it would collide with: this is a paste or a drop back
    // into the folder it already lives in, which duplicates or does nothing
    // rather than asking anything (SPEC_NAME_CONFLICT_COPY_MOVE 3-5).
    if (handles.count(entry.handle) > 0)
        return false;
    return entry.isFolder ? folders.count(entry.name) > 0 : files.count(entry.name) > 0;
}

std::vector<bool> FileMutationController::collidingEntries(const std::vector<NodeRef>& entries,
                                                           const DestinationSnapshot& destination)
{
    std::vector<bool> colliding;
    colliding.reserve(entries.size());
    // Keyed by kind as well as name, for the reason collidesWith matches by kind.
    std::set<std::pair<std::string, bool>> arriving;
    for (const NodeRef& entry : entries)
    {
        // Already a child of the destination: this is a paste or drop back into
        // its own folder, which duplicates rather than collides, and two such
        // entries duplicate independently (SPEC_NAME_CONFLICT_COPY_MOVE 3-5).
        if (destination.handles.count(entry.handle) > 0)
        {
            colliding.push_back(false);
            continue;
        }
        const bool broughtTwice = !arriving.insert({entry.name, entry.isFolder}).second;
        colliding.push_back(destination.collidesWith(entry) || broughtTwice);
    }
    return colliding;
}

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
                    if (result.errorCode == MegaErrorCode::kEExist)
                    {
                        mNotifications->notifyError(QStringLiteral("renameNameTaken"));
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
                emit favouriteChanged(handle, favourite);
            });
        });
}

void FileMutationController::copyLinkToClipboard(quint64 handle)
{
    mBusy->begin();
    mFileOps->exportLink(
        static_cast<std::uint64_t>(handle),
        [this, self = shared_from_this()](Result<std::string> result) {
            invokeOnGuiThread(this, [this, result = std::move(result)]() {
                mBusy->end();
                if (!result.success)
                {
                    qCWarning(lcFileOps)
                        << "exportLink failed:" << QString::fromStdString(result.errorMessage)
                        << "code=" << result.errorCode;
                    mNotifications->notifyError(QStringLiteral("copyLink"),
                                                result.errorCode,
                                                QString::fromStdString(result.errorMessage));
                    return;
                }
                // An export that succeeds with no URL would otherwise blank the
                // clipboard while claiming the link was copied.
                if (result.value().empty())
                {
                    mNotifications->notifyError(QStringLiteral("copyLinkEmpty"));
                    return;
                }
                // Via the cast, not a bare QGuiApplication::clipboard(): the unit-test
                // binary runs on a plain QCoreApplication, where building a QClipboard
                // finds no platform integration to talk to.
                if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr)
                    QGuiApplication::clipboard()->setText(
                        QString::fromStdString(result.value()));
                mNotifications->notifyOperation(QStringLiteral("copyLink"), 1, 0);
            });
        });
}

void FileMutationController::removeLink(quint64 handle)
{
    mBusy->begin();
    mFileOps->removeLink(
        static_cast<std::uint64_t>(handle),
        [this, self = shared_from_this()](Result<void> result) {
            invokeOnGuiThread(this, [this, result = std::move(result)]() {
                mBusy->end();
                if (!result.success)
                {
                    qCWarning(lcFileOps)
                        << "removeLink failed:" << QString::fromStdString(result.errorMessage)
                        << "code=" << result.errorCode;
                    mNotifications->notifyError(QStringLiteral("removeLink"),
                                                result.errorCode,
                                                QString::fromStdString(result.errorMessage));
                    return;
                }
                mNotifications->notifyOperation(QStringLiteral("removeLink"), 1, 0);
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
        [this, self = shared_from_this(), name](Result<void> result) {
            invokeOnGuiThread(this, [this, name, result = std::move(result)]() {
                // Above the four-way branching below on purpose: each of those
                // outcomes returns, so anything lower would be four chances to
                // leak the count.
                mBusy->end();
                if (result.success)
                {
                    // The name rides on the re-read, which is the listing the new
                    // folder will arrive in, so that it can be selected and
                    // scrolled to once it exists in the model.
                    mNavigation->refreshVisibleListing(name);
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

bool FileMutationController::folderNameTaken(const QString& name) const
{
    return mNavigation->hasChildFolderNamed(name.toStdString());
}

void FileMutationController::moveHandlesToRubbish(const QVariantList& handles)
{
    if (handles.isEmpty())
        return;

    // Read now, for the same reason moveEntriesTo does: a refresh mid-batch could
    // in principle move this tab.
    const quint64 source = mNavigation->currentHandle();
    const bool sourceIsRoot = mNavigation->atRoot();

    auto batch = mBulk.start("moveToRubbish",
                             static_cast<int>(handles.size()),
                             {},
                             [this, source, sourceIsRoot](int succeeded, int) {
                                 if (succeeded > 0)
                                     emit nodesRemoved(source, sourceIsRoot);
                             });

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

void FileMutationController::deleteHandlesPermanently(const QVariantList& handles)
{
    if (handles.isEmpty())
        return;

    // No nodesRemoved fan-out, matching restoreHandles: the only listing these rows
    // appear in is the Rubbish bin, and a second tab open on it at the same moment
    // is rare enough not to justify the signal.
    auto batch = mBulk.start("deletePermanently", static_cast<int>(handles.size()));

    for (const QVariant& handle : handles)
    {
        mFileOps->removeNode(static_cast<std::uint64_t>(handle.toULongLong()),
                             [this, self = shared_from_this(), batch](Result<void> result) {
                                 invokeOnGuiThread(this, [batch, result = std::move(result)]() {
                                     batch->settle(result);
                                 });
                             });
    }
}

void FileMutationController::emptyRubbishBin()
{
    // Re-opening rather than re-reading, and only on a Rubbish tab: this tab may be
    // showing a folder *inside* the bin, which the emptying destroys -- refreshing
    // it would re-read a folder that no longer exists. A tab on any other screen has
    // nothing to re-read, since the bin's contents are not visible from it.
    auto batch = mBulk.start("emptyRubbish", 1, [this]() {
        if (mNavigation->viewKind() == static_cast<int>(ViewKind::Rubbish))
            mNavigation->openRubbish();
    });

    mFileOps->emptyRubbishBin([this, self = shared_from_this(), batch](Result<void> result) {
        invokeOnGuiThread(this, [batch, result = std::move(result)]() {
            batch->settle(result);
        });
    });
}

void FileMutationController::restoreHandles(const QVariantList& handles)
{
    if (handles.isEmpty())
        return;

    // Resolved up front, before any move lands: restoring the parent folder of
    // another selected node would otherwise change where that node's target
    // resolves to mid-batch.
    struct Restore
    {
        std::uint64_t handle;
        RestoreTarget target;
    };
    std::vector<Restore> restores;
    int unresolvable = 0;
    for (const QVariant& handle : handles)
    {
        const auto raw = static_cast<std::uint64_t>(handle.toULongLong());
        Result<RestoreTarget> target = mFileOps->restoreTargetFor(raw);
        if (target.success)
            restores.push_back({raw, target.value()});
        else
            ++unresolvable;
    }

    if (restores.empty())
    {
        mNotifications->notifyOperation(QStringLiteral("restore"), 0, unresolvable);
        return;
    }

    const bool anyFellBackToRoot =
        std::any_of(restores.begin(), restores.end(), [](const Restore& restore) {
            return restore.target.fellBackToRoot;
        });

    // The whole selection, not just the resolvable part: the ones that could not be
    // resolved are settled as failures below so the tally still adds up to what the
    // user asked for. Only this tab is refreshed -- the destinations are per node
    // and a restore is rare enough not to justify a fan-out to the other tabs.
    auto batch = mBulk.start(anyFellBackToRoot ? "restoreToRoot" : "restore",
                             static_cast<int>(handles.size()));

    for (int i = 0; i < unresolvable; ++i)
        batch->settle(Result<void>::fail("node is gone", MegaErrorCode::kENoEnt));

    for (const Restore& restore : restores)
    {
        mFileOps->move(restore.handle,
                       restore.target.handle,
                       restore.target.isRoot,
                       std::string(),
                       [this, self = shared_from_this(), batch](Result<void> result) {
                           invokeOnGuiThread(this, [batch, result = std::move(result)]() {
                               batch->settle(result);
                           });
                       });
    }
}

void FileMutationController::moveEntriesTo(const QVariantList& entries,
                                           quint64 target,
                                           bool targetIsRoot)
{
    // A drag started in this tab, so this tab is where the nodes came from --
    // read *now*, because a refresh mid-batch could in principle move it.
    moveEntriesFrom(ClipboardController::toNodeRefs(entries),
                    target,
                    targetIsRoot,
                    mNavigation->currentHandle(),
                    mNavigation->atRoot(),
                    MoveConflict::Ask);
}

void FileMutationController::moveEntriesFrom(const std::vector<NodeRef>& entries,
                                             quint64 target,
                                             bool targetIsRoot,
                                             quint64 source,
                                             bool sourceIsRoot,
                                             MoveConflict onConflict)
{
    if (entries.empty())
        return;

    mBusy->begin();
    mService->listChildrenOf(
        static_cast<std::uint64_t>(target),
        targetIsRoot,
        kNameOrder,
        [this,
         self = shared_from_this(),
         entries,
         target,
         targetIsRoot,
         source,
         sourceIsRoot,
         onConflict](Result<std::vector<FileEntry>> result) {
            invokeOnGuiThread(
                this,
                [this,
                 self,
                 entries,
                 target,
                 targetIsRoot,
                 source,
                 sourceIsRoot,
                 onConflict,
                 result = std::move(result)]() mutable {
                    mBusy->end();
                    mCutPasteReadInFlight = false;
                    // No cached fallback, not even for a cut-paste
                    // into this tab's own folder: a move issued
                    // against names read from a stale listing is
                    // exactly the silent same-named sibling this
                    // question exists to prevent.
                    if (!result.success)
                    {
                        qCWarning(lcFileOps) << "move destination read failed:"
                                             << QString::fromStdString(result.errorMessage)
                                             << "code=" << result.errorCode;
                        mNotifications->notifyError(QStringLiteral("move"),
                                                    result.errorCode,
                                                    QString::fromStdString(result.errorMessage));
                        return;
                    }

                    startMoveBatch(entries,
                                   target,
                                   targetIsRoot,
                                   source,
                                   sourceIsRoot,
                                   DestinationSnapshot::of(result.value()),
                                   onConflict);
                });
        });
}

void FileMutationController::startMoveBatch(const std::vector<NodeRef>& entries,
                                            quint64 target,
                                            bool targetIsRoot,
                                            quint64 source,
                                            bool sourceIsRoot,
                                            DestinationSnapshot destination,
                                            MoveConflict onConflict)
{
    if (entries.empty())
        return;

    const std::vector<bool> colliding = collidingEntries(entries, destination);

    // A generated name has to dodge this batch's own arrivals, not just what the
    // destination already holds: an entry that keeps its name lands there too, and
    // MEGA would accept the duplicate without complaint.
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        if (!colliding[i])
            destination.taken.insert(entries[i].name);
    }

    if (onConflict == MoveConflict::Ask)
    {
        QStringList conflictingFiles;
        QStringList conflictingFolders;
        QStringList renamedFiles;
        QStringList renamedFolders;
        // Previewed against a copy of the same set the Rename plan below works
        // from, so the name shown is the name that answer would pick.
        std::set<std::string> preview = destination.taken;
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const NodeRef& entry = entries[i];
            if (!colliding[i])
                continue;
            const std::string chosen =
                FileOperationService::uniqueMoveName(entry.name, entry.isFolder, preview);
            preview.insert(chosen);
            (entry.isFolder ? conflictingFolders : conflictingFiles)
                .append(QString::fromStdString(entry.name));
            (entry.isFolder ? renamedFolders : renamedFiles).append(QString::fromStdString(chosen));
        }
        if (!conflictingFiles.isEmpty() || !conflictingFolders.isEmpty())
        {
            emit moveNameConflict(ClipboardController::toVariantList(entries),
                                  conflictingFiles,
                                  conflictingFolders,
                                  renamedFiles + renamedFolders,
                                  target,
                                  targetIsRoot,
                                  source,
                                  sourceIsRoot);
            return;
        }
        // Nothing collides, so every answer means the same thing here; Skip is the
        // one that issues every entry unchanged.
        onConflict = MoveConflict::Skip;
    }

    // Names are settled before the batch starts because the batch has to be told how
    // many moves to expect, and Skip removes entries from that count.
    struct PlannedMove
    {
        std::uint64_t handle;
        std::string newName;
    };
    std::vector<PlannedMove> plan;
    plan.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const NodeRef& entry = entries[i];
        if (colliding[i] && onConflict == MoveConflict::Skip)
            continue;
        if (colliding[i] && onConflict == MoveConflict::Rename)
        {
            const std::string chosen =
                FileOperationService::uniqueMoveName(entry.name, entry.isFolder, destination.taken);
            // Claimed right away: MEGA allows duplicate siblings, so two colliding
            // entries must not be handed the same new name.
            destination.taken.insert(chosen);
            plan.push_back({entry.handle, chosen});
            continue;
        }
        plan.push_back({entry.handle, std::string()});
    }

    if (plan.empty())
        return;

    clearClipboardIfSpentBy(entries);

    auto batch =
        mBulk.start("move",
                    static_cast<int>(plan.size()),
                    {},
                    [this, target, targetIsRoot, source, sourceIsRoot](int succeeded, int) {
                        if (succeeded > 0)
                            emit nodesMoved(target, targetIsRoot, source, sourceIsRoot);
                    });

    for (const PlannedMove& planned : plan)
        moveOne(planned.handle, target, targetIsRoot, planned.newName, batch);
}

void FileMutationController::clearClipboardIfSpentBy(const std::vector<NodeRef>& entries)
{
    if (!mClipboard->isCut())
        return;

    // Matched as a set, not just by size: a drag-move of other nodes must not
    // swallow a cut that is still waiting to be pasted.
    const std::vector<NodeRef>& cut = mClipboard->entries();
    if (cut.size() != entries.size())
        return;
    const bool sameNodes = std::all_of(cut.begin(), cut.end(), [&entries](const NodeRef& cutEntry) {
        return std::any_of(entries.begin(), entries.end(), [&cutEntry](const NodeRef& entry) {
            return entry.handle == cutEntry.handle;
        });
    });
    if (sameNodes)
        mClipboard->clear();
}

void FileMutationController::moveOne(std::uint64_t handle,
                                     quint64 target,
                                     bool targetIsRoot,
                                     const std::string& newName,
                                     std::shared_ptr<BulkOperationRunner::Batch> batch)
{
    mFileOps->move(handle,
                   static_cast<std::uint64_t>(target),
                   targetIsRoot,
                   newName,
                   [this, self = shared_from_this(), batch](Result<void> result) {
                       invokeOnGuiThread(this, [batch, result = std::move(result)]() {
                           batch->settle(result);
                       });
                   });
}

void FileMutationController::moveIgnoringExisting(const QVariantList& entries,
                                                  quint64 target,
                                                  bool targetIsRoot,
                                                  quint64 source,
                                                  bool sourceIsRoot)
{
    moveEntriesFrom(ClipboardController::toNodeRefs(entries),
                    target,
                    targetIsRoot,
                    source,
                    sourceIsRoot,
                    MoveConflict::Proceed);
}

void FileMutationController::moveRenamingExisting(const QVariantList& entries,
                                                  quint64 target,
                                                  bool targetIsRoot,
                                                  quint64 source,
                                                  bool sourceIsRoot)
{
    moveEntriesFrom(ClipboardController::toNodeRefs(entries),
                    target,
                    targetIsRoot,
                    source,
                    sourceIsRoot,
                    MoveConflict::Rename);
}

void FileMutationController::moveSkippingExisting(const QVariantList& entries,
                                                  quint64 target,
                                                  bool targetIsRoot,
                                                  quint64 source,
                                                  bool sourceIsRoot)
{
    moveEntriesFrom(ClipboardController::toNodeRefs(entries),
                    target,
                    targetIsRoot,
                    source,
                    sourceIsRoot,
                    MoveConflict::Skip);
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
        // Ctrl+V twice before the destination read lands would otherwise issue the
        // same cut as two batches, the second one asking the user to resolve a
        // collision against the nodes the first just moved. The conflict dialog
        // covers the rest of the wait by being modal.
        if (mCutPasteReadInFlight)
            return;
        mCutPasteReadInFlight = true;

        // Copied out because the move outlives the clipboard: clearClipboardIfSpentBy
        // empties it once the batch is issued, which is after the destination read
        // and after any conflict dialog. A copy keeps its content either way, so
        // pasting twice is a legitimate way to get two copies.
        const std::vector<NodeRef> cut = mClipboard->entries();
        const quint64 source = mClipboard->sourceHandle();
        const bool sourceIsRoot = mClipboard->sourceIsRoot();
        moveEntriesFrom(cut, target, targetIsRoot, source, sourceIsRoot, MoveConflict::Ask);
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
            invokeOnGuiThread(
                this, [this, self, target, targetIsRoot, result = std::move(result)]() mutable {
                    mBusy->end();
                    const std::vector<NodeRef>& entries = mClipboard->entries();
                    if (entries.empty())
                        return; // cleared while the destination read was in flight

                    // A failed read is no reason to refuse: the destination *is*
                    // this tab's folder, so its cached listing is the best answer.
                    startCopyBatch(entries,
                                   target,
                                   targetIsRoot,
                                   DestinationSnapshot::of(result.success
                                                               ? result.value()
                                                               : mNavigation->cachedChildren()),
                                   CopyConflict::Ask);
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
                this,
                [this, self, copied, target, targetIsRoot, result = std::move(result)]() mutable {
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

                    startCopyBatch(copied,
                                   target,
                                   targetIsRoot,
                                   DestinationSnapshot::of(result.value()),
                                   CopyConflict::Ask);
                });
        });
}

void FileMutationController::startCopyBatch(const std::vector<NodeRef>& entries,
                                            quint64 target,
                                            bool targetIsRoot,
                                            DestinationSnapshot destination,
                                            CopyConflict onConflict)
{
    if (entries.empty())
        return;

    const std::vector<bool> colliding = collidingEntries(entries, destination);

    if (onConflict == CopyConflict::Ask)
    {
        QStringList conflictingFiles;
        QStringList conflictingFolders;
        QStringList renamedFiles;
        QStringList renamedFolders;
        // Walks every entry, not just the colliding ones, because the Rename plan
        // below does: a non-colliding entry claims a name too, and a preview that
        // skipped it could advertise a name that answer would then find taken.
        std::set<std::string> preview = destination.taken;
        // Sizing walks each entry's sub-tree, so it is gated on the question being
        // asked at all: without this every ordinary conflict-free paste would pay
        // for a total it then discards. A size the SDK cannot answer is left out of
        // the total rather than failing the question -- it only costs the dialog a
        // parenthetical.
        const bool anyColliding = std::find(colliding.begin(), colliding.end(), true) !=
                                  colliding.end();
        std::uint64_t conflictingBytes = 0;
        std::uint64_t unaffectedBytes = 0;
        for (std::size_t i = 0; i < entries.size(); ++i)
        {
            const NodeRef& entry = entries[i];
            const std::string chosen =
                FileOperationService::uniqueCopyName(entry.name, entry.isFolder, preview);
            preview.insert(chosen);
            if (anyColliding)
            {
                const Result<std::uint64_t> size = mFileOps->subtreeSizeOf(entry.handle);
                (colliding[i] ? conflictingBytes : unaffectedBytes) +=
                    size.success ? size.value() : 0;
            }
            if (!colliding[i])
                continue;
            (entry.isFolder ? conflictingFolders : conflictingFiles)
                .append(QString::fromStdString(entry.name));
            (entry.isFolder ? renamedFolders : renamedFiles).append(QString::fromStdString(chosen));
        }
        if (!conflictingFiles.isEmpty() || !conflictingFolders.isEmpty())
        {
            emit copyNameConflict(ClipboardController::toVariantList(entries),
                                  conflictingFiles,
                                  conflictingFolders,
                                  renamedFiles + renamedFolders,
                                  sizeText(conflictingBytes),
                                  sizeText(unaffectedBytes),
                                  target,
                                  targetIsRoot);
            return;
        }
        onConflict = CopyConflict::Rename;
    }

    // Names are settled before the batch starts because the batch has to be told
    // how many copies to expect, and Skip removes entries from that count.
    struct PlannedCopy
    {
        std::uint64_t handle;
        std::string newName;
    };
    std::vector<PlannedCopy> plan;
    plan.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        const NodeRef& entry = entries[i];
        if (colliding[i] && onConflict == CopyConflict::Skip)
            continue;
        if (colliding[i] && onConflict == CopyConflict::Proceed)
        {
            // Empty == keep the source's name. For a file that is what makes the SDK
            // attach the copy as a new version over the existing one; for a folder it
            // lands a second folder of that name, since copyNode neither merges nor
            // versions (SPEC_NAME_CONFLICT_COPY_MOVE 1-2).
            plan.push_back({entry.handle, std::string()});
            continue;
        }

        const std::string chosen =
            FileOperationService::uniqueCopyName(entry.name, entry.isFolder, destination.taken);
        // Claimed right away: MEGA allows duplicate siblings, so two clipboard
        // entries can share a name and must not be handed the same new one.
        destination.taken.insert(chosen);
        plan.push_back({entry.handle, chosen == entry.name ? std::string() : chosen});
    }

    if (plan.empty())
        return;

    // Only the destination gained anything, so re-read this tab only when it is
    // the destination -- which is always true for a paste and usually false for
    // a Ctrl+drop. Every other tab showing it is reached through nodesCopied.
    auto batch = mBulk.start(
        "copy",
        static_cast<int>(plan.size()),
        [this, target, targetIsRoot]() {
            mNavigation->refreshIfShowing(target, targetIsRoot);
        },
        [this, target, targetIsRoot](int succeeded, int) {
            if (succeeded > 0)
                emit nodesCopied(target, targetIsRoot);
        });

    for (const PlannedCopy& copy : plan)
    {
        mFileOps->copy(copy.handle,
                       static_cast<std::uint64_t>(target),
                       targetIsRoot,
                       copy.newName,
                       [this, self = shared_from_this(), batch](Result<void> result) {
                           invokeOnGuiThread(this, [batch, result = std::move(result)]() {
                               batch->settle(result);
                           });
                       });
    }
}

void FileMutationController::copyIgnoringExisting(const QVariantList& entries,
                                                  quint64 target,
                                                  bool targetIsRoot)
{
    answerCopyConflict(entries, target, targetIsRoot, CopyConflict::Proceed);
}

void FileMutationController::copyRenamingExisting(const QVariantList& entries,
                                                  quint64 target,
                                                  bool targetIsRoot)
{
    answerCopyConflict(entries, target, targetIsRoot, CopyConflict::Rename);
}

void FileMutationController::copySkippingExisting(const QVariantList& entries,
                                                  quint64 target,
                                                  bool targetIsRoot)
{
    answerCopyConflict(entries, target, targetIsRoot, CopyConflict::Skip);
}

void FileMutationController::answerCopyConflict(const QVariantList& entries,
                                                quint64 target,
                                                bool targetIsRoot,
                                                CopyConflict resolution)
{
    const std::vector<NodeRef> chosen = ClipboardController::toNodeRefs(entries);
    if (chosen.empty())
        return;

    const Result<void> allowed =
        mFileOps->canAddChildren(static_cast<std::uint64_t>(target), targetIsRoot);
    if (!allowed.success)
    {
        qCWarning(lcFileOps) << "copy rejected:" << QString::fromStdString(allowed.errorMessage)
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
        [this, self = shared_from_this(), chosen, target, targetIsRoot, resolution](
            Result<std::vector<FileEntry>> result) {
            invokeOnGuiThread(
                this,
                [this,
                 self,
                 chosen,
                 target,
                 targetIsRoot,
                 resolution,
                 result = std::move(result)]() mutable {
                    mBusy->end();
                    // No cached fallback even for a paste: the answer is only
                    // meaningful against the listing it was asked about, and
                    // "Continue" issued against a guess is what versions over the
                    // wrong file.
                    if (!result.success)
                    {
                        qCWarning(lcFileOps) << "copy destination re-read failed:"
                                             << QString::fromStdString(result.errorMessage)
                                             << "code=" << result.errorCode;
                        mNotifications->notifyError(QStringLiteral("copy"),
                                                    result.errorCode,
                                                    QString::fromStdString(result.errorMessage));
                        return;
                    }

                    startCopyBatch(chosen,
                                   target,
                                   targetIsRoot,
                                   DestinationSnapshot::of(result.value()),
                                   resolution);
                });
        });
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
