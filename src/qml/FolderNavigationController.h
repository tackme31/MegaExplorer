#pragma once
#include "core/FileOperationService.h"
#include "core/FolderNavigationService.h"
#include "core/SearchService.h"
#include "core/SortOrder.h"
// Not just forward-declared like NotificationController below: startCopyBatch
// takes ClipboardController::Entry, which a drag-copy fills in without the
// clipboard being involved at all.
#include "ClipboardController.h"
#include "FileListModel.h"

#include <QObject>
#include <QTimer>
#include <QVariantList>

#include <functional>
#include <memory>
#include <set>
#include <string>

class NotificationController;

// Q_INVOKABLE entry points below fire off SDK-thread callbacks that outlive
// any single call; enable_shared_from_this + shared_from_this() captures in
// those callbacks keep `this` alive until the callback runs, even if the
// owning tab is closed mid-fetch (see TabsController.h's lifetime writeup
// for the full rationale -- Phase 9 introduced per-tab instances of this
// controller, so it's no longer guaranteed to outlive every in-flight
// callback the way a single app-lifetime instance was).
//
// Staying alive is only half of it: that shared_from_this() copy lives in a
// closure the SDK's listener destroys on the SDK thread, so it can be the
// last reference and would run ~QTimer (mBusyDelayTimer below) there.
// Instances are therefore created through GuiThread.h's makeGuiOwned, which
// sends the destruction back to the GUI thread -- see REFACTOR_PLANS.md's
// R2-5.

// QML-facing GUI glue wrapping FolderNavigationService + SearchService +
// FileListModel. QML can't pass C++ callbacks, so the Q_INVOKABLE entry
// points below are fire-and-forget: internally they hand the service a bound
// lambda, marshal its result onto the GUI thread, then update the owned
// FileListModel and canGoBack.
//
// DownloadController deliberately never touches FileListModel (stays
// decoupled from folder navigation). ThumbnailController is an intentional
// exception to that: it needs to update visible rows in place, so
// fileListModelForThumbnails() below hands it shared ownership of the same
// FileListModel instance this controller uses.
class FolderNavigationController : public QObject,
                                   public std::enable_shared_from_this<FolderNavigationController>
{
    Q_OBJECT
    Q_PROPERTY(QObject* fileListModel READ fileListModel CONSTANT)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY canGoBackChanged)
    // Derived from mBreadcrumb like atRoot below, hence the shared NOTIFY.
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY breadcrumbChanged)
    // Elements are QVariantMap{"name", "handle", "isRoot"}, root-first,
    // current folder last -- QML composes display labels itself (e.g. the
    // root's "Cloud Drive" label), C++ only supplies structured fields.
    Q_PROPERTY(QVariantList breadcrumb READ breadcrumb NOTIFY breadcrumbChanged)
    // Both derived from mBreadcrumb's last element -- backs TabStrip.qml's
    // tab titles (TabsController relays this controller's breadcrumbChanged
    // into a per-row dataChanged()). atRoot is true (and name empty) before
    // the breadcrumb has ever resolved, so a brand-new tab's title reads as
    // "Cloud Drive" rather than blank.
    Q_PROPERTY(QString currentFolderName READ currentFolderName NOTIFY breadcrumbChanged)
    Q_PROPERTY(bool atRoot READ atRoot NOTIFY breadcrumbChanged)
    // Same derivation as currentFolderName/atRoot above -- backs
    // FolderTreePanel.qml's highlight (Phase 10): 0 when the breadcrumb
    // hasn't resolved yet or the current location is the root (meaningless
    // sentinel handle, same convention as PathSegment::isRoot).
    Q_PROPERTY(quint64 currentHandle READ currentHandle NOTIFY breadcrumbChanged)
    // True while this tab has a mutating operation or a server sync in flight
    // -- backs TabStrip.qml's per-tab spinner (TabsController relays the
    // signal into a per-row dataChanged(), same as breadcrumbChanged above).
    // Deliberately NOT set by folder listing / search / breadcrumb fetches:
    // those are synchronous in-memory reads of the SDK's node tree
    // (IMegaClient::getChildren) and finish before anything could repaint.
    // Uploads aren't here either, for the opposite reason -- they belong to no
    // tab, so TabsController ORs them into the role by destination instead.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit FolderNavigationController(std::shared_ptr<FolderNavigationService> navigationService,
                                        std::shared_ptr<SearchService> searchService,
                                        std::shared_ptr<FileOperationService> fileOperationService,
                                        NotificationController* notifications,
                                        ClipboardController* clipboard,
                                        QObject* parent = nullptr);

    QObject* fileListModel();

    // Shared ownership of the same FileListModel instance as fileListModel()
    // above, for main.cpp's composition root to hand to ThumbnailController.
    // Not Q_INVOKABLE/not QML-facing -- QML only ever needs the QObject*
    // property for its view's model:.
    //
    // Shared rather than a bare pointer because TabContext destroys
    // thumbnails before navigation, while a thumbnail fetch in flight keeps
    // ThumbnailController alive past its tab: a raw pointer here would leave
    // its queued setThumbnailPath() writing into a freed model
    // (REFACTOR_PLANS.md's R2-4).
    std::shared_ptr<FileListModel> fileListModelForThumbnails();

    bool canGoBack() const;

    bool busy() const;

    // False both before the breadcrumb has resolved and at the root, so the
    // "up" button is disabled in exactly the cases where there's no parent.
    bool canGoUp() const;

    QVariantList breadcrumb() const;

    QString currentFolderName() const;
    bool atRoot() const;
    quint64 currentHandle() const;

    // Not Q_INVOKABLE: called once from main.cpp's composition root (via
    // AuthController::authStateChanged reaching LoggedIn), not from QML.
    Q_INVOKABLE void loadRoot();

    Q_INVOKABLE void openFolder(quint64 handle);
    Q_INVOKABLE void goBack();

    // Navigates to the breadcrumb's second-to-last entry, i.e. the current
    // folder's parent. No-op when !canGoUp(). Routed through navigateTo, so
    // it's pushed onto the back-stack -- Explorer's "up" is undoable by
    // "back" too.
    Q_INVOKABLE void goUp();

    // Breadcrumb segment click. isRoot mirrors PathSegment::isRoot -- handle
    // is meaningless when it's true (same sentinel convention used
    // throughout, see PathSegment.h).
    Q_INVOKABLE void navigateTo(quint64 handle, bool isRoot);

    // Empty query clears the search and restores the last folder listing
    // (from mLastFolderEntries, no server round-trip). A non-empty query
    // runs a recursive search scoped to the currently open folder; results
    // replace the list but don't touch navigation state (back-stack/
    // canGoBack), so opening a folder from search results still works via
    // the existing openFolder().
    Q_INVOKABLE void search(QString query);

    // column: 0=Name, 1=ModificationTime, 2=Size (FileTableView.qml's 3-column
    // layout). Called both from the header-click handler and once at startup
    // with the Settings-restored value (see mHasLoadedOnce below) -- QML's
    // Component.onCompleted fires before login/loadRoot() have run.
    Q_INVOKABLE void setSortOrder(int column, bool ascending);

    // Clears all navigation/listing state back to a fresh, pre-login state.
    // Called on AuthController::authStateChanged reaching LoggedOut (sign
    // out, or a definitively-invalid restored session) so a subsequent
    // login -- possibly a different account -- never briefly shows the
    // previous account's cached listing or retains its back-stack handles.
    Q_INVOKABLE void reset();

    // Inline-rename commit (F2 / context menu, see InlineRenameField.qml).
    // Callers must skip this when newName equals the current name -- this
    // controller doesn't track per-row names. A rename never changes the
    // handle, so the selection survives the refetch below.
    Q_INVOKABLE void renameEntry(quint64 handle, const QString& newName);

    // Moves every currently selected entry into the Rubbish bin, one SDK call
    // per entry, and reports the tally once through
    // NotificationController::notifyOperation. No undo: IMegaClient
    // deliberately exposes no general move, so there'd be nothing to undo
    // with (see docs/PROGRESS.md's Phase 12 log).
    Q_INVOKABLE void moveSelectionToRubbish();

    // Creates a folder inside the one this tab is showing (NewFolderDialog.qml
    // -> the FolderBackground menu). The parent is read off currentHandle()/
    // atRoot() rather than passed in: unlike a drag & drop destination, this
    // action can only ever target the view it was opened from.
    //
    // Reports through folderCreated/folderCreationFailed *and* the usual
    // NotificationController, split by whose problem it is: the two failures
    // the user can fix by editing the name (kEExist, kEArgs) only get the
    // signal, so the dialog can stay open and say so inline, while everything
    // else gets a toast and lets the dialog close.
    Q_INVOKABLE void createFolder(const QString& name);

    // Drag & drop's move. handles is the selection snapshot taken when the drag
    // gesture started, passed in explicitly rather than read back off
    // mFileListModel like moveSelectionToRubbish does: a drop can land on the
    // folder tree or a quick-access pin, both of which are shared by every tab,
    // so "the selection" there would be ambiguous. target/targetIsRoot use the
    // usual isRoot sentinel convention.
    Q_INVOKABLE void moveHandlesTo(const QVariantList& handles, quint64 target, bool targetIsRoot);

    // Pastes whatever is on the app-global clipboard into the folder this tab
    // is showing. Like createFolder above, the destination is read off
    // currentHandle()/atRoot() rather than passed in: paste only ever targets
    // the view it was invoked from (background menu / Ctrl+V).
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
    // Two-stage like paste()'s copy branch, and for the same reason -- but the
    // destination is read with FolderNavigationService::listChildrenOf, since
    // it is whatever folder the pointer was over rather than this tab's own.
    // A destination read that fails aborts the whole drop: unlike paste(),
    // there is no cached listing of *that* folder to fall back on, and copying
    // under an unverified name is what silently versions over an existing file.
    Q_INVOKABLE void copyEntriesTo(const QVariantList& entries, quint64 target, bool targetIsRoot);

    // canDropHandlesOn's copy counterpart, and all-or-nothing in the same way.
    // Differs in which refusals apply: a copy into the folder the nodes already
    // live in is legitimate (it duplicates them), while a copy of a folder into
    // its own subtree is not -- see FileOperationService::canCopy.
    Q_INVOKABLE bool
    canCopyEntriesOn(const QVariantList& entries, quint64 target, bool targetIsRoot) const;

    // Toolbar refresh button / F5. Asks the API for anything it hasn't told us
    // yet (FolderNavigationService::syncWithServer), then re-reads whatever
    // this tab is showing; no-op until the first successful load (see
    // mHasLoadedOnce). The sync is what makes this more than a re-read of the
    // node tree we already have -- getChildren alone can only ever return what
    // the SDK happens to have been told already, so without it the button
    // guarantees nothing about freshness.
    Q_INVOKABLE void refresh();

    // Re-reads the listing, but only if this tab is the one showing
    // (handle, isRoot), so an app-global controller can fan a "something
    // changed in folder X" notification out to every tab and let each tab
    // decide for itself (see UploadController::destinationChanged).
    // Not refresh(): the caller is reporting a change this app just made, so
    // the SDK already knows about it, and syncing once per showing tab would
    // be that many pointless round-trips.
    Q_INVOKABLE void refreshIfShowing(quint64 handle, bool isRoot);

signals:
    void canGoBackChanged();
    void breadcrumbChanged();
    void busyChanged();

    void folderCreated();
    // reason is a structured selector, not a message: "exists" (a folder of
    // that name is already there -- the server's answer, see
    // IMegaClient::createFolder), "invalidName", or "other". Same
    // C++-supplies-structure / QML-supplies-wording split as
    // NotificationController's context strings.
    void folderCreationFailed(QString reason);

    // At least one node of a moveHandlesTo batch landed. source is where this
    // tab was standing when the drag started. This controller has already
    // refreshed itself; the signal exists so TabsController can fan
    // refreshIfShowing out to the *other* tabs, which is what makes a
    // cross-tab drop show up on the destination tab that is sitting right
    // there in view. Both ends are reported because a move empties one folder
    // and fills another.
    void nodesMoved(quint64 destination, bool destinationIsRoot, quint64 source, bool sourceIsRoot);

    // At least one node of a paste-copy landed. Same purpose as nodesMoved
    // above, but only the destination is reported: a copy leaves the folder the
    // nodes came from untouched.
    void nodesCopied(quint64 destination, bool destinationIsRoot);

private:
    void applyResult(Result<std::vector<FileEntry>> result);
    void applySearchResult(Result<std::vector<FileEntry>> result);
    void refreshCurrentFolder();

    // "Re-fetch whatever the user is currently looking at" -- the folder
    // listing, or the active search's results (plus, in that case, the cached
    // folder listing behind them, so clearing the search afterwards doesn't
    // show a stale one). Shared by setSortOrder and by the Phase 12 mutations,
    // which must not silently drop the user out of a search.
    void refreshVisibleListing();

    // refreshVisibleListing() behind the mHasLoadedOnce guard -- the old body
    // of refresh(), split out when refresh() grew its server sync so that
    // refreshIfShowing could keep the plain re-read.
    void refreshListingIfLoaded();

    // The only two places mBusyCount is allowed to change. Every mutating
    // entry point pairs exactly one begin with one end per SDK call it makes
    // (so a bulk fan-out of N calls is N pairs), the end going at the very top
    // of the callback, before any of its own branching -- createFolder's four
    // outcomes would otherwise be four chances to leak the count.
    void beginBusyOperation();
    void endBusyOperation();

    // Resolves the current location's ancestor chain and updates
    // mBreadcrumb. Called at the end of applyResult's success path (loadRoot
    // / openFolder / goBack / navigateTo / refreshCurrent all funnel through
    // it), so the breadcrumb tracks the actual folder hierarchy rather than
    // navigation history. Only emits breadcrumbChanged if the resolved path
    // actually differs, so refreshCurrent (e.g. a sort-order change) doesn't
    // needlessly rebuild the Breadcrumb.qml Repeater.
    void refreshBreadcrumb();

    // Shared bookkeeping for one bulk fan-out (moveSelectionToRubbish,
    // moveHandlesTo): only the last callback to land refreshes the listing and
    // reports the tally, so N operations produce one refetch and one
    // notification. Same shape as QuickAccessModel's Sweep.
    struct BulkOperationBatch
    {
        int remaining = 0;
        int succeeded = 0;
        int failed = 0;
        // Run once the batch empties, after the refresh and the notification.
        // moveHandlesTo uses it to announce where the nodes went; empty for
        // moveSelectionToRubbish, which has nowhere to announce.
        std::function<void(const BulkOperationBatch&)> onComplete;
        // Overrides the default "re-read what this tab is showing". A copy
        // leaves the source folder alone, so a Ctrl+drop onto some other folder
        // has nothing to re-read here -- and refreshVisibleListing() is a full
        // model reset (plus a recursive search re-run, if one is showing).
        // Empty means the default.
        std::function<void()> refresh;
    };

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
    // (paste() falls back to its cached listing of the folder it is standing
    // in, copyEntriesTo() has no such listing and refuses). Split out at all
    // only because that read is asynchronous -- a stale set would let a copy
    // land as a new *version* of an existing file rather than beside it.
    void startCopyBatch(const std::vector<ClipboardController::Entry>& entries,
                        quint64 target,
                        bool targetIsRoot,
                        std::set<std::string> taken);

    // Common tail of both bulk fan-outs above: counts one outcome and, once the
    // batch is empty, refreshes and reports it under context. Must run on the
    // GUI thread (callers wrap it in invokeOnGuiThread).
    void accountForBulkOutcome(const std::shared_ptr<BulkOperationBatch>& batch,
                               const Result<void>& result,
                               const char* context);

    std::shared_ptr<FolderNavigationService> mService;
    std::shared_ptr<SearchService> mSearchService;
    std::shared_ptr<FileOperationService> mFileOps;
    NotificationController* mNotifications;
    ClipboardController* mClipboard;
    std::shared_ptr<FileListModel> mFileListModel;
    QVariantList mBreadcrumb;
    std::vector<FileEntry> mLastFolderEntries; // restored when search is cleared
    SortOrder mSortOrder{SortKey::Name, true};
    std::string mLastSearchQuery; // empty == not currently searching
    // loadRoot() is called only after engine.loadFromModule() has already
    // run QML's Component.onCompleted, which restores the persisted sort via
    // setSortOrder() -- guards against that startup call re-fetching (and
    // erroring out) before login/fetchNodes have ever run. Set true once
    // applyResult sees its first success; reset back to false by reset().
    bool mHasLoadedOnce = false;
    // In-flight mutating operations / server syncs. Separate from the
    // published busy() below, which only turns true once mBusyDelayTimer has
    // fired: an operation that finishes inside the delay never shows a
    // spinner at all, which is the point.
    int mBusyCount = 0;
    bool mBusyVisible = false;
    QTimer mBusyDelayTimer;
};
