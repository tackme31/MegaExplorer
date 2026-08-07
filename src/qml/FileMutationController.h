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

// Everything that changes the remote node tree from one tab: rename, new
// folder, rubbish, move, copy, paste. Split out of FolderNavigationController
// (REFACTOR_PLANS.md's R5-1), which had grown to hold three unrelated state
// machines and a third of src/qml's whole QML API surface.
//
// Like the navigation half, the Q_INVOKABLE entry points below are
// fire-and-forget and their SDK-thread callbacks outlive the call, so
// enable_shared_from_this + shared_from_this() captures keep `this` alive past
// a mid-flight tab close, and instances are created through GuiThread.h's
// makeGuiOwned so the destruction lands on the GUI thread (R2-5).
//
// mNavigation is a *strong* reference, and that is load-bearing rather than
// convenient. Before the split both halves were one object, so
// invokeOnGuiThread's "drop the queued call if the target died" covered both at
// once. Now a copy batch can still be in flight when the tab closes: this
// controller stays alive through its own shared_from_this() capture while the
// navigation half, holding nothing in flight, would be freed -- and settling
// the batch calls back into it to refresh the listing. Owning it makes "the
// navigation half outlives this one" a fact of the type rather than an argument
// about destruction order. The cost is that a closed tab's navigation
// controller and FileListModel survive until the batch finishes and then
// refresh a model nobody is showing, which is exactly what happened before the
// split.
//
// Deliberately has no reset(): unlike the navigation half there is no per-tab
// state to clear. mFileOps is stateless, mClipboard is app-global (clearing it
// on one tab's logout would be wrong), mBulk keeps nothing between batches --
// an in-flight Batch is owned by the callbacks carrying it -- and the busy
// counter is shared, so FolderNavigationController::reset()'s abandonAll()
// already covers it.
class FileMutationController : public QObject,
                               public std::enable_shared_from_this<FileMutationController>
{
    Q_OBJECT

public:
    // navigation is where "the folder this tab is showing" comes from for the
    // three actions that can only target the view they were invoked from
    // (createFolder, paste, canPaste), and where a finished mutation goes to
    // have the listing re-read.
    //
    // navigationService is the same per-tab instance the navigation half holds,
    // but only listChildrenOf() is ever called on it -- a read that touches no
    // service state -- so nothing here depends on where that service thinks the
    // user currently is.
    explicit FileMutationController(std::shared_ptr<FolderNavigationController> navigation,
                                    std::shared_ptr<FolderNavigationService> navigationService,
                                    std::shared_ptr<FileOperationService> fileOperationService,
                                    std::shared_ptr<BusyState> busy,
                                    NotificationController* notifications,
                                    ClipboardController* clipboard,
                                    QObject* parent = nullptr);

    // Inline-rename commit (F2 / context menu, see InlineRenameField.qml).
    // Callers must skip this when newName equals the current name -- nothing
    // here tracks per-row names. A rename never changes the handle, so the
    // selection survives the refetch afterwards.
    Q_INVOKABLE void renameEntry(quint64 handle, const QString& newName);

    // Moves every handle into the Rubbish bin, one SDK call per handle, and
    // reports the tally once through NotificationController::notifyOperation.
    // No undo: IMegaClient deliberately exposes no general move, so there'd be
    // nothing to undo with (see docs/PROGRESS.md's Phase 12 log).
    //
    // Handles rather than "the current selection" so this matches the other
    // four batch entry points, and so ConfirmRubbishDialog can pass the same
    // snapshot it worded its prompt from -- a background refresh can prune the
    // selection while that dialog is open (R5-1).
    Q_INVOKABLE void moveHandlesToRubbish(const QVariantList& handles);

    // Creates a folder inside the one this tab is showing (NewFolderDialog.qml
    // -> the FolderBackground menu). The parent is read off the navigation
    // half's currentHandle()/atRoot() rather than passed in: unlike a drag &
    // drop destination, this action can only ever target the view it was
    // opened from.
    //
    // Reports through folderCreated/folderCreationFailed *and* the usual
    // NotificationController, split by whose problem it is: the two failures
    // the user can fix by editing the name (kEExist, kEArgs) only get the
    // signal, so the dialog can stay open and say so inline, while everything
    // else gets a toast and lets the dialog close.
    Q_INVOKABLE void createFolder(const QString& name);

    // Drag & drop's move. handles is the selection snapshot taken when the drag
    // gesture started, passed in explicitly because a drop can land on the
    // folder tree or a quick-access pin, both of which are shared by every tab,
    // so "the selection" there would be ambiguous. target/targetIsRoot use the
    // usual isRoot sentinel convention.
    Q_INVOKABLE void moveHandlesTo(const QVariantList& handles, quint64 target, bool targetIsRoot);

    // Pastes whatever is on the app-global clipboard into the folder this tab
    // is showing. Like createFolder above, the destination is read off the
    // navigation half rather than passed in: paste only ever targets the view
    // it was invoked from (background menu / Ctrl+V).
    //
    // A cut is Phase 14a's move, reported under the same "move" context but
    // announcing the *clipboard's* source folder rather than this tab's, so the
    // folder the nodes were cut from refreshes even when the paste happened in
    // another tab. A copy is a two-stage fan-out: re-read the destination's
    // names, then one copy per entry under a name nothing there is using (see
    // IMegaClient::copyNode for why a colliding one is not merely untidy).
    //
    // Silent when there is nothing to do -- empty clipboard, or a cut going
    // back into its own folder, both of which canPaste() already greys out.
    Q_INVOKABLE void paste();

    // What the background menu greys its Paste entry on, sampled when the menu
    // opens. Synchronous all the way down (ClipboardController's own state plus
    // FileOperationService::canAddChildren), so it's safe to call from there.
    Q_INVOKABLE bool canPaste() const;

    // Whether every handle could be moved onto target -- what a hovered drop
    // target paints its accept/reject feedback from. Synchronous all the way
    // down to IMegaClient::checkMove, so it's safe to call from a hover
    // handler. False for an empty selection: nothing to drop.
    Q_INVOKABLE bool
    canDropHandlesOn(const QVariantList& handles, quint64 target, bool targetIsRoot) const;

    // Ctrl+drag's copy. entries carries the same {handle, name, isFolder} maps
    // the clipboard takes, not bare handles like the move above: a copy has to
    // know the source names to pick non-colliding ones, and re-resolving every
    // handle at drop time would buy nothing.
    //
    // Two-stage like paste()'s copy branch, and reading the destination the
    // same way -- but a destination read that fails aborts the whole drop:
    // unlike paste(), the destination is whatever folder the pointer was over,
    // so there is no cached listing of it to fall back on, and copying under an
    // unverified name is what silently versions over an existing file.
    Q_INVOKABLE void copyEntriesTo(const QVariantList& entries, quint64 target, bool targetIsRoot);

    // canDropHandlesOn's copy counterpart, and all-or-nothing in the same way.
    // Differs in which refusals apply: a copy into the folder the nodes already
    // live in is legitimate (it duplicates them), while a copy of a folder into
    // its own subtree is not -- see FileOperationService::canCopy.
    Q_INVOKABLE bool
    canCopyEntriesOn(const QVariantList& entries, quint64 target, bool targetIsRoot) const;

signals:
    void folderCreated();
    // reason is a structured selector, not a message: "exists" (a folder of
    // that name is already there -- the server's answer, see
    // IMegaClient::createFolder), "invalidName", or "other". Same
    // C++-supplies-structure / QML-supplies-wording split as
    // NotificationController's context strings.
    void folderCreationFailed(QString reason);

    // At least one node of a moveHandlesTo batch landed. source is where this
    // tab was standing when the drag started. This tab has already refreshed
    // itself; the signal exists so TabsController can fan refreshIfShowing out
    // to the *other* tabs, which is what makes a cross-tab drop show up on the
    // destination tab that is sitting right there in view. Both ends are
    // reported because a move empties one folder and fills another.
    void nodesMoved(quint64 destination, bool destinationIsRoot, quint64 source, bool sourceIsRoot);

    // At least one node of a paste-copy landed. Same purpose as nodesMoved
    // above, but only the destination is reported: a copy leaves the folder the
    // nodes came from untouched.
    void nodesCopied(quint64 destination, bool destinationIsRoot);

private:
    // Body of moveHandlesTo, with "where did these come from" passed in rather
    // than read off this tab. A drag knows its source is this tab; a cut-paste
    // knows it is wherever the clipboard was filled, which may be another tab
    // or a folder this one has since navigated away from.
    void moveHandlesFrom(const QVariantList& handles,
                         quint64 target,
                         bool targetIsRoot,
                         quint64 source,
                         bool sourceIsRoot);

    // Whether every clipboard entry could be *copied* into the folder this tab
    // is showing, carrying the first refusal so paste() can report it. Split
    // from canPaste() because paste() needs the reason, not just the verdict.
    Result<void> clipboardCopyAllowedHere() const;

    // Second stage of a copy, on the GUI thread. taken is the set of names the
    // destination already uses; how it was arrived at is the caller's problem,
    // because the two callers answer a failed destination read differently
    // (paste() falls back to the navigation half's cached listing of the folder
    // it is standing in, copyEntriesTo() has no such listing and refuses).
    // Split out at all only because that read is asynchronous -- a stale set
    // would let a copy land as a new *version* of an existing file rather than
    // beside it.
    void startCopyBatch(const std::vector<NodeRef>& entries,
                        quint64 target,
                        bool targetIsRoot,
                        std::set<std::string> taken);

    std::shared_ptr<FolderNavigationController> mNavigation;
    std::shared_ptr<FolderNavigationService> mService;
    std::shared_ptr<FileOperationService> mFileOps;
    NotificationController* mNotifications;
    ClipboardController* mClipboard;
    // The navigation half's counter, shared: QML sees busy as one bool per tab
    // (navigation.busy), and TabsController ORs that with uploads by
    // destination. Nothing here publishes it.
    std::shared_ptr<BusyState> mBusy;
    BulkOperationRunner mBulk;
};
