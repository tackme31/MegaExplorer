#pragma once
#include "BulkOperationRunner.h"
#include "BusyState.h"
#include "core/FileOperationService.h"
#include "core/FolderNavigationService.h"
#include "core/NodeRef.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
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
// mBulk keeps nothing between batches, the busy counter is cleared by
// FolderNavigationController::reset()'s abandonAll(), and mCutPasteReadInFlight
// self-clears when the read it guards lands.
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

    // moveHandlesToRubbish' inverse, for the Rubbish bin screen. Each node goes back
    // to the folder it was binned from; one whose original folder is gone lands in
    // the Cloud Drive root instead, and the toast says so rather than leaving the
    // user to find it (IMegaClient::getRestoreTarget).
    Q_INVOKABLE void restoreHandles(const QVariantList& handles);

    // Destroys the nodes outright, for the Rubbish bin screen. Irreversible, so the
    // caller confirms first; same handles-not-selection contract as
    // moveHandlesToRubbish above, and for the same reason.
    Q_INVOKABLE void deleteHandlesPermanently(const QVariantList& handles);

    // Destroys the whole bin in one request, whatever this tab is showing -- the
    // action is offered from the side panel too, where there is no listing to read a
    // selection from.
    Q_INVOKABLE void emptyRubbishBin();

    // Creates a folder inside the one this tab is showing; the parent is read off
    // the navigation half, since this action can only target the view it opened
    // from.
    //
    // Failures are split by whose problem they are: the two the user can fix by
    // editing the name (kEExist, kEArgs) only raise folderCreationFailed, so the
    // dialog stays open and says so inline; everything else also gets a toast.
    Q_INVOKABLE void createFolder(const QString& name);

    // Advisory only, for warning while the name is still being typed: it reads the
    // cached listing, which a background refresh or another client can outdate, so
    // createFolder's kEExist stays the authority (IMegaClient::createFolder). Don't
    // turn this into a gate on the Ok button -- a stale "taken" would then block a
    // name the server would have accepted.
    Q_INVOKABLE bool folderNameTaken(const QString& name) const;

    // Drag & drop's move. entries carries {handle, name, isFolder} maps rather
    // than bare handles because a move has to know what it would land on: the
    // destination is read first, and a name already used there raises
    // moveNameConflict rather than quietly producing two same-named siblings,
    // which MEGA allows. Passed explicitly because a drop can land on the folder
    // tree or a quick-access pin -- both shared by every tab, so "the selection"
    // is ambiguous there.
    Q_INVOKABLE void moveEntriesTo(const QVariantList& entries, quint64 target, bool targetIsRoot);

    // Pastes the app-global clipboard into the folder this tab is showing.
    //
    // A cut is reported under the "move" context but announces the *clipboard's*
    // source folder, so the folder the nodes were cut from refreshes even when the
    // paste happened in another tab; it is otherwise the same two-stage shape as a
    // copy, asking at moveNameConflict. A copy is two-stage: re-read the
    // destination's names, then copy each entry under a name nothing there uses
    // (IMegaClient::copyNode says why a colliding one is not merely untidy). An
    // entry whose name is already taken stops the batch at copyNameConflict --
    // unless it is being pasted back into the folder it already lives in, which
    // is a duplication rather than a collision and asks nothing.
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

    // The two answers to copyNameConflict, both taking the batch back rather than
    // reading state kept here: the question rides on the dialog, as the upload
    // ones do. Cancel calls neither.
    //
    // Replacing means copying under the source's own name, which the SDK turns
    // into a new *version* of the existing file (IMegaClient::copyNode) -- the
    // closest thing MEGA has to an overwrite. It reaches files only: copyNode
    // cannot merge into an existing folder, so a colliding folder is skipped
    // under either answer.
    Q_INVOKABLE void
    copyReplacingExisting(const QVariantList& entries, quint64 target, bool targetIsRoot);
    Q_INVOKABLE void
    copySkippingExisting(const QVariantList& entries, quint64 target, bool targetIsRoot);

    // moveNameConflict's two answers, shaped like the copy pair above and riding
    // on the dialog for the same reason. They carry the source folder as well
    // because a move empties one folder and fills another, and the source of a
    // cut-paste is wherever the clipboard was filled -- not recoverable here by
    // the time the question is answered.
    //
    // Replacing bins the node in the way and then moves: moveNode has no
    // versioning counterpart to copyNode's, so without that the destination
    // would end up holding both (SPEC_NAME_CONFLICT_RESOLUTION 1-3). It reaches
    // files only -- a colliding folder is skipped under either answer, as on the
    // copy path.
    Q_INVOKABLE void moveReplacingExisting(const QVariantList& entries,
                                           quint64 target,
                                           bool targetIsRoot,
                                           quint64 source,
                                           bool sourceIsRoot);
    Q_INVOKABLE void moveSkippingExisting(const QVariantList& entries,
                                          quint64 target,
                                          bool targetIsRoot,
                                          quint64 source,
                                          bool sourceIsRoot);

signals:
    void folderCreated();
    // reason is a structured selector, not a message: "exists", "invalidName" or
    // "other". C++ supplies structure, QML supplies wording.
    void folderCreationFailed(QString reason);

    // At least one node of a moveEntriesTo batch landed. This tab has already
    // refreshed itself; the signal exists so TabsController can fan refreshIfShowing
    // out to the *other* tabs. Both ends are reported because a move empties one
    // folder and fills another.
    void nodesMoved(quint64 destination, bool destinationIsRoot, quint64 source, bool sourceIsRoot);

    // Same purpose as nodesMoved, destination only: a copy leaves the source alone.
    void nodesCopied(quint64 destination, bool destinationIsRoot);

    // A copy would land on names the destination already uses, so nothing has been
    // issued yet -- one of the two copy*Existing() calls above (or nothing, for
    // Cancel) decides what happens. Files and folders are reported apart because
    // only a file can be replaced: the dialog offers Replace exactly when
    // conflictingFiles is non-empty, and a colliding folder is skipped whatever
    // the answer (docs/investigations/SPEC_NAME_CONFLICT_RESOLUTION.md 3-1).
    void copyNameConflict(QVariantList entries,
                          QStringList conflictingFiles,
                          QStringList conflictingFolders,
                          quint64 destination,
                          bool destinationIsRoot);

    // copyNameConflict's move counterpart -- same question, different verbs and
    // a different pair of answers, so the dialog handles both. Kept a separate
    // signal rather than a discriminated one so neither carries parameters the
    // other has no meaning for: this one adds the source folder, which only a
    // move has to announce afterwards.
    void moveNameConflict(QVariantList entries,
                          QStringList conflictingFiles,
                          QStringList conflictingFolders,
                          quint64 destination,
                          bool destinationIsRoot,
                          quint64 source,
                          bool sourceIsRoot);

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
    // What to do with an entry whose name the destination already holds. Ask is
    // the entry points' value; Replace and Skip are the dialog's two answers.
    // There is no Rename here: unlike a copy, a move has no reason to invent a
    // name -- the user asked for the node to be *there*, under that name.
    enum class MoveConflict
    {
        Ask,
        Replace,
        Skip
    };

    // Body of moveEntriesTo, with the source passed in: a cut-paste's source is
    // wherever the clipboard was filled, possibly another tab or a folder this one
    // has since navigated away from. Reads the destination, then hands over to
    // startMoveBatch; also the body of the two answers, which re-read so the
    // answer is applied against whatever the folder holds now.
    void moveEntriesFrom(const std::vector<NodeRef>& entries,
                         quint64 target,
                         bool targetIsRoot,
                         quint64 source,
                         bool sourceIsRoot,
                         MoveConflict onConflict);

    // canPaste()'s copy check, carrying the first refusal: paste() needs the
    // reason, not just the verdict.
    Result<void> clipboardCopyAllowedHere() const;

    // What to do with an entry whose name the destination already holds. Ask is
    // the entry points' value; Replace and Skip are the dialog's two answers.
    // Rename is not offered any more, but stays as the engine every non-colliding
    // copy runs through -- and as the whole answer for a same-folder paste.
    enum class CopyConflict
    {
        Ask,
        Rename,
        Replace,
        Skip
    };

    // The destination's listing, reduced to what a copy or a move needs. taken is
    // every name already there (what a generated name must dodge); files and
    // folders are the halves an entry could land on, kept apart because only a
    // file can be replaced; handles is who already lives there, so an entry
    // pasted back into its own folder is recognised as a duplication rather than
    // a collision (docs/investigations/SPEC_NAME_CONFLICT_RESOLUTION.md 3-2).
    //
    // files carries the handle as well because a replacing *move* has to bin the
    // node in the way first. MEGA allows duplicate siblings, so a name can have
    // several; the first wins, which matches what copyNode's versioning reaches.
    struct DestinationSnapshot
    {
        std::set<std::string> taken;
        std::map<std::string, std::uint64_t> files;
        std::set<std::string> folders;
        std::set<std::uint64_t> handles;

        static DestinationSnapshot of(const std::vector<FileEntry>& children);

        // Matched by kind: a file and a folder sharing a name can neither
        // overwrite nor merge into each other, so asking about that pair would
        // pose a question with no useful answer.
        bool collidesWith(const NodeRef& entry) const;
    };

    // Second stage of a copy, on the GUI thread. How the snapshot was arrived at
    // is the caller's problem, since the callers answer a failed destination read
    // differently (paste() falls back to the cached listing, the other two
    // refuse). A stale one would let a copy land as a new *version* of an existing
    // file rather than beside it.
    void startCopyBatch(const std::vector<NodeRef>& entries,
                        quint64 target,
                        bool targetIsRoot,
                        DestinationSnapshot destination,
                        CopyConflict onConflict);

    // Body of the three copy*Existing() answers: re-reads the destination and
    // resumes the batch under the chosen resolution. Re-read rather than carried
    // through the dialog so the answer sees anything that changed while the
    // question was open, which also means a failed read has to end the copy.
    void answerCopyConflict(const QVariantList& entries,
                            quint64 target,
                            bool targetIsRoot,
                            CopyConflict resolution);

    // startCopyBatch's move twin, on the GUI thread: decides per entry whether it
    // collides, raises moveNameConflict if any does and onConflict is still Ask,
    // and otherwise issues one moveOne() per surviving entry.
    void startMoveBatch(const std::vector<NodeRef>& entries,
                        quint64 target,
                        bool targetIsRoot,
                        quint64 source,
                        bool sourceIsRoot,
                        const DestinationSnapshot& destination,
                        MoveConflict onConflict);

    // Empties the clipboard when the batch about to be issued *is* its cut, matched
    // by handle so an unrelated drag-move leaves the cut alone. Called from the
    // issuing point rather than from paste(): the conflict dialog sits between the
    // two, and cancelling it must leave the cut intact.
    void clearClipboardIfSpentBy(const std::vector<NodeRef>& entries);

    // One planned move, settling the batch exactly once however many requests it
    // takes. replaced is the node this one is to overwrite, which has to reach
    // the rubbish bin before the move -- and if that fails the move is abandoned
    // rather than left to land beside the file it was meant to replace.
    void moveOne(std::uint64_t handle,
                 std::optional<std::uint64_t> replaced,
                 quint64 target,
                 bool targetIsRoot,
                 std::shared_ptr<BulkOperationRunner::Batch> batch);

    // True between a cut-paste and its destination read landing. The clipboard used
    // to double as that guard by being emptied at once; it now survives until the
    // batch is issued, so the re-entry it blocked needs its own flag. Cleared by any
    // destination read, which is harmless: the ones that are not this paste's can
    // only land after it began.
    bool mCutPasteReadInFlight = false;

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
