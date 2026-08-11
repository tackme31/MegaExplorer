#pragma once
#include "BulkOperationRunner.h"
#include "BusyState.h"
#include "core/FileOperationService.h"
#include "core/FolderNavigationService.h"
#include "core/NodeRef.h"

#include <QObject>
#include <QVariantList>

#include <memory>
#include <set>
#include <string>

class ClipboardController;
class FolderNavigationController;
class NotificationController;

// Everything that changes the remote node tree from one tab: rename, new folder,
// rubbish, move, copy, paste.
//
// The Q_INVOKABLE entry points are fire-and-forget and their SDK-thread callbacks
// outlive the call, so they capture shared_from_this() to survive a mid-flight tab
// close, and instances are created through GuiThread.h's makeGuiOwned so
// destruction lands on the GUI thread.
//
// mNavigation is deliberately a *strong* reference: a copy batch still in flight
// when the tab closes keeps this controller alive through its own capture, while
// the navigation half -- holding nothing in flight -- would otherwise be freed
// before the batch calls back into it to refresh the listing. The cost is that a
// closed tab's navigation controller and FileListModel survive to refresh a model
// nobody is showing.
//
// Deliberately has no reset(): mFileOps is stateless, mClipboard is app-global,
// mBulk keeps nothing between batches, and the busy counter is cleared by
// FolderNavigationController::reset()'s abandonAll().
class FileMutationController : public QObject,
                               public std::enable_shared_from_this<FileMutationController>
{
    Q_OBJECT

public:
    // navigation supplies "the folder this tab is showing" for the actions that
    // can only target it (createFolder, paste, canPaste) and re-reads the listing
    // afterwards. navigationService is the same per-tab instance, but only
    // listChildrenOf() is called on it -- a read that touches no service state.
    explicit FileMutationController(std::shared_ptr<FolderNavigationController> navigation,
                                    std::shared_ptr<FolderNavigationService> navigationService,
                                    std::shared_ptr<FileOperationService> fileOperationService,
                                    std::shared_ptr<BusyState> busy,
                                    NotificationController* notifications,
                                    ClipboardController* clipboard,
                                    QObject* parent = nullptr);

    // Callers must skip this when newName equals the current name -- nothing here
    // tracks per-row names.
    Q_INVOKABLE void renameEntry(quint64 handle, const QString& newName);

    // No toast on success: a heart is toggled often enough that one per click
    // would be noise. The flag is written straight into this tab's model rather
    // than re-read (see FolderNavigationController::applyFavouriteChange), and
    // announced to the other tabs through favouriteChanged below.
    Q_INVOKABLE void setEntryFavourite(quint64 handle, bool favourite);

    // One SDK call per handle, tallied once through
    // NotificationController::notifyOperation.
    //
    // Takes handles rather than "the current selection" so ConfirmRubbishDialog can
    // pass the same snapshot it worded its prompt from -- a background refresh can
    // prune the selection while that dialog is open.
    Q_INVOKABLE void moveHandlesToRubbish(const QVariantList& handles);

    // Creates a folder inside the one this tab is showing; the parent is read off
    // the navigation half, since this action can only target the view it opened
    // from.
    //
    // Failures are split by whose problem they are: the two the user can fix by
    // editing the name (kEExist, kEArgs) only raise folderCreationFailed, so the
    // dialog stays open and says so inline; everything else also gets a toast.
    Q_INVOKABLE void createFolder(const QString& name);

    // Drag & drop's move. handles is the snapshot taken when the drag started,
    // passed explicitly because a drop can land on the folder tree or a
    // quick-access pin -- both shared by every tab, so "the selection" is
    // ambiguous there.
    Q_INVOKABLE void moveHandlesTo(const QVariantList& handles, quint64 target, bool targetIsRoot);

    // Pastes the app-global clipboard into the folder this tab is showing.
    //
    // A cut is reported under the "move" context but announces the *clipboard's*
    // source folder, so the folder the nodes were cut from refreshes even when the
    // paste happened in another tab. A copy is two-stage: re-read the
    // destination's names, then copy each entry under a name nothing there uses
    // (IMegaClient::copyNode says why a colliding one is not merely untidy).
    //
    // Silent when there is nothing to do -- canPaste() already greys those out.
    Q_INVOKABLE void paste();

    // Synchronous all the way down, so the background menu can sample it while
    // opening.
    Q_INVOKABLE bool canPaste() const;

    // What a hovered drop target paints its accept/reject feedback from, so it is
    // synchronous all the way down to IMegaClient::checkMove. False for an empty
    // selection.
    Q_INVOKABLE bool
    canDropHandlesOn(const QVariantList& handles, quint64 target, bool targetIsRoot) const;

    // Ctrl+drag's copy. entries carries {handle, name, isFolder} maps rather than
    // bare handles because a copy needs the source names to pick non-colliding
    // ones.
    //
    // Two-stage like paste()'s copy branch, but a failed destination read aborts
    // the drop: the destination is whatever folder the pointer was over, so there
    // is no cached listing to fall back on, and copying under an unverified name
    // is what silently versions over an existing file.
    Q_INVOKABLE void copyEntriesTo(const QVariantList& entries, quint64 target, bool targetIsRoot);

    // canDropHandlesOn's copy counterpart. Refusals differ: copying into the folder
    // the nodes already live in is legitimate (it duplicates them), copying a
    // folder into its own subtree is not.
    Q_INVOKABLE bool
    canCopyEntriesOn(const QVariantList& entries, quint64 target, bool targetIsRoot) const;

signals:
    void folderCreated();
    // reason is a structured selector, not a message: "exists", "invalidName" or
    // "other". C++ supplies structure, QML supplies wording.
    void folderCreationFailed(QString reason);

    // At least one node of a moveHandlesTo batch landed. This tab has already
    // refreshed itself; the signal exists so TabsController can fan refreshIfShowing
    // out to the *other* tabs. Both ends are reported because a move empties one
    // folder and fills another.
    void nodesMoved(quint64 destination, bool destinationIsRoot, quint64 source, bool sourceIsRoot);

    // Same purpose as nodesMoved, destination only: a copy leaves the source alone.
    void nodesCopied(quint64 destination, bool destinationIsRoot);

    // Source only, for the opposite reason: the rubbish bin is not a place any tab
    // can be showing. Until F7b this batch announced nothing at all, so a second
    // tab on the same folder -- or on the favourites listing, which any rubbished
    // node drops out of -- kept showing rows that were gone.
    void nodesRemoved(quint64 source, bool sourceIsRoot);

    // Same purpose again, for the flag rather than the tree. Carries no location
    // because a favourites listing spans the whole drive and a folder listing
    // recognises the node by handle alone.
    void favouriteChanged(quint64 handle, bool favourite);

private:
    // Body of moveHandlesTo, with the source passed in: a cut-paste's source is
    // wherever the clipboard was filled, possibly another tab or a folder this one
    // has since navigated away from.
    void moveHandlesFrom(const QVariantList& handles,
                         quint64 target,
                         bool targetIsRoot,
                         quint64 source,
                         bool sourceIsRoot);

    // canPaste()'s copy check, carrying the first refusal: paste() needs the
    // reason, not just the verdict.
    Result<void> clipboardCopyAllowedHere() const;

    // Second stage of a copy, on the GUI thread. taken is the set of names the
    // destination already uses; how it was arrived at is the caller's problem,
    // since the two callers answer a failed destination read differently (paste()
    // falls back to the cached listing, copyEntriesTo() refuses). A stale set would
    // let a copy land as a new *version* of an existing file rather than beside it.
    void startCopyBatch(const std::vector<NodeRef>& entries,
                        quint64 target,
                        bool targetIsRoot,
                        std::set<std::string> taken);

    std::shared_ptr<FolderNavigationController> mNavigation;
    std::shared_ptr<FolderNavigationService> mService;
    std::shared_ptr<FileOperationService> mFileOps;
    NotificationController* mNotifications;
    ClipboardController* mClipboard;
    // The navigation half's counter, shared: QML sees one busy bool per tab, and
    // nothing here publishes it.
    std::shared_ptr<BusyState> mBusy;
    BulkOperationRunner mBulk;
};
