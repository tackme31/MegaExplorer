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

    // Puts the node's public link on the system clipboard, creating the link first if
    // it has none -- so this is both "share this" and "give me that link again", and
    // the menu needs no export-state flag to pick between them.
    //
    // The clipboard write happens here rather than in QML because Qt Quick exposes no
    // clipboard API at all.
    Q_INVOKABLE void copyLinkToClipboard(quint64 handle);

    // Stops the sharing copyLinkToClipboard started. Silent about whether a link
    // existed: IMegaClient::disableExport reports success either way, so a stale menu
    // cannot produce a failure toast for a node that was already unshared.
    Q_INVOKABLE void removeLink(quint64 handle);

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

    // The three answers to copyNameConflict, all taking the batch back rather than
    // reading state kept here: the question rides on the dialog, as the upload
    // ones do. Cancel calls none of them.
    //
    // Ignoring means issuing the copy under the source's own name. For a file the SDK
    // forces that into a new *version* of the same-named one (IMegaClient::copyNode)
    // -- MEGA has no way to end up with two same-named files; for a folder it lands a
    // second folder of that name, since copyNode neither merges nor versions one.
    // Renaming takes the "... - Copy" name uniqueCopyName picks.
    Q_INVOKABLE void
    copyIgnoringExisting(const QVariantList& entries, quint64 target, bool targetIsRoot);
    Q_INVOKABLE void
    copyRenamingExisting(const QVariantList& entries, quint64 target, bool targetIsRoot);
    Q_INVOKABLE void
    copySkippingExisting(const QVariantList& entries, quint64 target, bool targetIsRoot);

    // moveNameConflict's three answers, shaped like the copy trio above and riding
    // on the dialog for the same reason. They carry the source folder as well
    // because a move empties one folder and fills another, and the source of a
    // cut-paste is wherever the clipboard was filled -- not recoverable here by
    // the time the question is answered.
    //
    // Ignoring issues the move unchanged. moveNode looks at no name at all, so
    // both end up side by side, folders included -- MEGA has no move that
    // overwrites, and synthesising one out of copy + rubbish is what this path
    // stopped doing (SPEC_NAME_CONFLICT_COPY_MOVE 1-2, 7-2). Renaming takes the
    // "... (2)" name uniqueMoveName picks, in the same request as the move.
    Q_INVOKABLE void moveIgnoringExisting(const QVariantList& entries,
                                          quint64 target,
                                          bool targetIsRoot,
                                          quint64 source,
                                          bool sourceIsRoot);
    Q_INVOKABLE void moveRenamingExisting(const QVariantList& entries,
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
    // issued yet -- one of the three copy*Existing() calls above (or nothing, for
    // Cancel) decides what happens. Files and folders are reported apart because
    // "Continue" resolves differently for them: a file becomes a new version of the
    // existing one, a folder a second folder of that name
    // (docs/investigations/SPEC_NAME_CONFLICT_COPY_MOVE.md 1-2).
    //
    // renamedTo pairs with conflictingFiles + conflictingFolders concatenated, so
    // the dialog can name what "Rename" would produce rather than promise an
    // unstated one (SPEC 3-4). It is a preview: the answer re-reads the destination
    // and picks again, which is what keeps a name claimed meanwhile from being
    // reused.
    //
    // The two sizes are already formatted, because QML has no formattedDataSize
    // equivalent; either is empty when it is zero or could not be read, and the
    // dialog then leaves that parenthetical out. Only the copy signal carries them:
    // a move rearranges nodes inside one account and adds nothing to it.
    void copyNameConflict(QVariantList entries,
                          QStringList conflictingFiles,
                          QStringList conflictingFolders,
                          QStringList renamedTo,
                          QString conflictingSize,
                          QString unaffectedSize,
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
                          QStringList renamedTo,
                          quint64 destination,
                          bool destinationIsRoot,
                          quint64 source,
                          bool sourceIsRoot);

    // Not a question: the copy or move was refused before anything was issued,
    // because the set itself brings one name twice. Carries only the repeated
    // names ({handle, name, isFolder} each), which is all the report shows.
    void duplicateNamesRejected(QVariantList entries);

    // Source only, for the opposite reason: the rubbish bin is not a place any tab
    // can be showing. Until F7b this batch announced nothing at all, so a second
    // tab on the same folder -- or on the favourites listing, which any rubbished
    // node drops out of -- kept showing rows that were gone.
    void nodesRemoved(quint64 source, bool sourceIsRoot);

    // Same purpose again, for the flag rather than the tree. Carries no location
    // because a favourites listing spans the whole drive and a folder listing
    // recognises the node by handle alone.
    void favouriteChanged(quint64 handle, bool favourite);

    // favouriteChanged's counterpart for the public-link flag, carrying no location
    // for the same reason. Fired by both copyLinkToClipboard (which exports when the
    // node has no link yet) and removeLink -- from the row's point of view the two
    // are the same event in opposite directions.
    void exportChanged(quint64 handle, bool exported);

private:
    // This tab's row plus every other tab's, in one call -- the three link
    // outcomes all have to do both.
    void markExported(quint64 handle, bool exported);

    // What to do with an entry whose name the destination already holds. Ask is
    // the entry points' value; the other three are the dialog's answers. Unlike
    // the copy side's, Rename here is only ever an answer -- a move that collides
    // with nothing keeps every name as it was.
    enum class MoveConflict
    {
        Ask,
        Proceed,
        Rename,
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
    // the entry points' value; the other three are the dialog's answers. Rename is
    // also the engine every non-colliding copy runs through -- and the whole answer
    // for a same-folder paste, which asks nothing (SPEC 3-5).
    enum class CopyConflict
    {
        Ask,
        Rename,
        Proceed,
        Skip
    };

    // The destination's listing, reduced to what a copy or a move needs. taken is
    // every name already there (what a generated name must dodge); files and
    // folders are the halves an entry could land on, kept apart because the two
    // are worded differently and only the copy path can act on a folder at all;
    // handles is who already lives there, so an entry pasted back into its own
    // folder is recognised as a duplication rather than a collision
    // (docs/investigations/SPEC_NAME_CONFLICT_COPY_MOVE.md 3-5).
    struct DestinationSnapshot
    {
        std::set<std::string> taken;
        std::set<std::string> files;
        std::set<std::string> folders;
        std::set<std::uint64_t> handles;

        static DestinationSnapshot of(const std::vector<FileEntry>& children);

        // Matched by kind: a file and a folder sharing a name can neither
        // overwrite nor merge into each other, so asking about that pair would
        // pose a question with no useful answer.
        bool collidesWith(const NodeRef& entry) const;
    };

    // One flag per entry, in order: does it land on a name that is already spoken
    // for? Shared by both paths so the dialog's preview and the plan it previews
    // never disagree. Only the destination's own names count -- a set that brings
    // one name twice is refused by duplicateArrivals() before this runs.
    static std::vector<bool> collidingEntries(const std::vector<NodeRef>& entries,
                                              const DestinationSnapshot& destination);

    // The names this set brings more than once, one entry per repeated name. Such
    // a set is refused outright rather than asked about: copying it stacks the
    // second item as a version of the first and moving it leaves two same-named
    // siblings, neither of which is what selecting both meant. Closes
    // SPEC_NAME_CONFLICT_COPY_MOVE 6-1.
    static std::vector<NodeRef> duplicateArrivals(const std::vector<NodeRef>& entries,
                                                  const DestinationSnapshot& destination);

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
                        DestinationSnapshot destination,
                        MoveConflict onConflict);

    // Empties the clipboard when the batch about to be issued *is* its cut, matched
    // by handle so an unrelated drag-move leaves the cut alone. Called from the
    // issuing point rather than from paste(): the conflict dialog sits between the
    // two, and cancelling it must leave the cut intact.
    void clearClipboardIfSpentBy(const std::vector<NodeRef>& entries);

    // One planned move, settling the batch exactly once. newName empty keeps the
    // node's name, which is every case but the dialog's Rename answer.
    void moveOne(std::uint64_t handle,
                 quint64 target,
                 bool targetIsRoot,
                 const std::string& newName,
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
