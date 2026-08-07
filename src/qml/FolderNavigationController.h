#pragma once
#include "core/FolderNavigationService.h"
#include "core/SearchService.h"
#include "core/SortOrder.h"
#include "BusyState.h"
#include "FileListModel.h"

#include <QObject>
#include <QVariantList>

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
// last reference and would destroy this QObject -- and the FileListModel it
// owns -- there. Instances are therefore created through GuiThread.h's
// makeGuiOwned, which sends the destruction back to the GUI thread -- see
// REFACTOR_PLANS.md's R2-5. The QTimer that made this acute now sits behind
// the shared BusyState, which is makeGuiOwned for the same reason (R5-1).

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
                                        std::shared_ptr<BusyState> busy,
                                        NotificationController* notifications,
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

    // Not Q_INVOKABLE: QML reaches the root load through
    // TabsController::loadRootAll(), never this per-tab entry point.
    void loadRoot();

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

    // The three below are FileMutationController's view of this one, and it is
    // their only consumer -- public rather than Q_INVOKABLE/Q_PROPERTY on
    // purpose, so nothing in QML binds to them (R5-1).

    // "Re-fetch whatever the user is currently looking at" -- the folder
    // listing, or the active search's results (plus, in that case, the cached
    // folder listing behind them, so clearing the search afterwards doesn't
    // show a stale one). Shared by setSortOrder and by the Phase 12 mutations,
    // which must not silently drop the user out of a search.
    void refreshVisibleListing();

    // Whether a listing has ever loaded. What the mutation half gates paste on:
    // before the first load there is no folder for it to target.
    bool isLoaded() const;

    // The child names of the folder this tab is showing, from the cached
    // listing rather than the server. The mutation half's fallback when a
    // paste's destination re-read fails -- the destination *is* this folder, so
    // this is the best answer available (see FileMutationController::paste).
    std::set<std::string> cachedChildNames() const;

signals:
    void canGoBackChanged();
    void breadcrumbChanged();
    void busyChanged();

private:
    void applyResult(Result<std::vector<FileEntry>> result);
    void applySearchResult(Result<std::vector<FileEntry>> result);
    void refreshCurrentFolder();

    // refreshVisibleListing() behind the mHasLoadedOnce guard -- the old body
    // of refresh(), split out when refresh() grew its server sync so that
    // refreshIfShowing could keep the plain re-read.
    void refreshListingIfLoaded();

    // Resolves the current location's ancestor chain and updates
    // mBreadcrumb. Called at the end of applyResult's success path (loadRoot
    // / openFolder / goBack / navigateTo / refreshCurrent all funnel through
    // it), so the breadcrumb tracks the actual folder hierarchy rather than
    // navigation history. Only emits breadcrumbChanged if the resolved path
    // actually differs, so refreshCurrent (e.g. a sort-order change) doesn't
    // needlessly rebuild the Breadcrumb.qml Repeater.
    void refreshBreadcrumb();

    std::shared_ptr<FolderNavigationService> mService;
    std::shared_ptr<SearchService> mSearchService;
    NotificationController* mNotifications;
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
    // Publishes busy() above; owns the delay before a spinner appears. Shared
    // rather than owned: one counter per tab, written by this controller's
    // refresh()/reset() and by the mutation side too (R5-1).
    std::shared_ptr<BusyState> mBusy;
};
