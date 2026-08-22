#pragma once
#include "core/FolderNavigationService.h"
#include "core/SearchService.h"
#include "core/SortOrder.h"
#include "BusyState.h"
#include "FileListModel.h"

#include <QObject>
#include <QVariantList>

#include <memory>
#include <optional>
#include <set>
#include <string>

class NotificationController;

// QML-facing GUI glue wrapping FolderNavigationService + SearchService +
// FileListModel. QML can't pass C++ callbacks, so the Q_INVOKABLE entry points
// are fire-and-forget: they hand the service a bound lambda, marshal its result
// onto the GUI thread, then update the owned FileListModel and canGoBack.
//
// Those callbacks outlive any single call and a tab can close mid-fetch, so they
// capture shared_from_this(). That copy then lives in a closure the SDK's listener
// destroys on the SDK thread, where it can be the last reference and would destroy
// this QObject -- and the FileListModel it owns -- off the GUI thread. Instances
// are therefore created through GuiThread.h's makeGuiOwned.
//
// DownloadController deliberately never touches FileListModel. ThumbnailController
// is the intentional exception: it updates visible rows in place, so
// fileListModelForThumbnails() hands it shared ownership of the same instance.
class FolderNavigationController : public QObject,
                                   public std::enable_shared_from_this<FolderNavigationController>
{
    Q_OBJECT
    Q_PROPERTY(QObject* fileListModel READ fileListModel CONSTANT)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY canGoBackChanged)
    // Derived from mBreadcrumb like atRoot below, hence the shared NOTIFY.
    Q_PROPERTY(bool canGoUp READ canGoUp NOTIFY breadcrumbChanged)
    // QVariantMap{"name", "handle", "isRoot", "kind"} elements, root-first. QML composes
    // the display labels (e.g. the root's "Cloud Drive"); C++ supplies fields only.
    Q_PROPERTY(QVariantList breadcrumb READ breadcrumb NOTIFY breadcrumbChanged)
    // Both derived from mBreadcrumb's last element. atRoot is true (and the name
    // empty) before the breadcrumb ever resolves, so a brand-new tab's title reads
    // as "Cloud Drive" rather than blank.
    Q_PROPERTY(QString currentFolderName READ currentFolderName NOTIFY breadcrumbChanged)
    Q_PROPERTY(bool atRoot READ atRoot NOTIFY breadcrumbChanged)
    // Same derivation; 0 when the breadcrumb hasn't resolved yet or the location is
    // the root (handle meaningless there, as everywhere).
    Q_PROPERTY(quint64 currentHandle READ currentHandle NOTIFY breadcrumbChanged)
    // Same derivation again. int rather than ViewKindEnum::Kind so this header keeps
    // its Qt-free core includes; QML compares against ViewKind.CloudDrive either way.
    Q_PROPERTY(int viewKind READ viewKind NOTIFY breadcrumbChanged)
    // Whether the column header may pick this tab's order. False on Recents, which
    // is defined by its own order (newest first) and has nothing to say sorted any
    // other way. Same derivation as viewKind, hence the shared NOTIFY.
    Q_PROPERTY(bool canSort READ canSort NOTIFY breadcrumbChanged)
    // Whether a search query or an advanced-search filter is narrowing what the model
    // holds, so an empty listing can say which kind of empty it is. Its own NOTIFY:
    // the query changes without the location doing so.
    Q_PROPERTY(bool searchActive READ searchActive NOTIFY searchActiveChanged)
    // The advanced-search facets this tab is searching with. The popup that edits them is
    // one control shared by every tab, so it has to read them back per tab rather than
    // remember what it last pushed. ints for the same reason viewKind is one.
    Q_PROPERTY(int searchFilterNodeType READ searchFilterNodeType NOTIFY searchFilterChanged)
    Q_PROPERTY(int searchFilterCategory READ searchFilterCategory NOTIFY searchFilterChanged)
    Q_PROPERTY(
        int searchFilterCreatedWithin READ searchFilterCreatedWithin NOTIFY searchFilterChanged)
    Q_PROPERTY(
        bool searchFilterFavouritesOnly READ searchFilterFavouritesOnly NOTIFY searchFilterChanged)
    // True while this tab has a mutating operation or a server sync in flight.
    // Deliberately NOT set by listing/search/breadcrumb fetches -- those are
    // synchronous in-memory reads and finish before anything could repaint. Nor by
    // uploads, which belong to no tab; TabsController ORs those in by destination.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit FolderNavigationController(std::shared_ptr<FolderNavigationService> navigationService,
                                        std::shared_ptr<SearchService> searchService,
                                        std::shared_ptr<BusyState> busy,
                                        NotificationController* notifications,
                                        QObject* parent = nullptr);

    QObject* fileListModel();

    // The same FileListModel instance as fileListModel(), for the composition root
    // to hand to ThumbnailController. Shared rather than a bare pointer because
    // TabContext destroys thumbnails before navigation, while a fetch in flight
    // keeps ThumbnailController alive past its tab -- a raw pointer would leave its
    // queued setThumbnailPath() writing into a freed model.
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
    int viewKind() const;
    bool canSort() const;
    bool searchActive() const;
    int searchFilterNodeType() const;
    int searchFilterCategory() const;
    int searchFilterCreatedWithin() const;
    bool searchFilterFavouritesOnly() const;

    // Not Q_INVOKABLE: QML reaches the root load through
    // TabsController::loadRootAll(), never this per-tab entry point.
    void loadRoot();

    Q_INVOKABLE void openFolder(quint64 handle);
    Q_INVOKABLE void goBack();

    // Routed through navigateTo, so "up" lands on the back-stack and "back" undoes
    // it, as in Explorer.
    Q_INVOKABLE void goUp();

    Q_INVOKABLE void navigateTo(quint64 handle, bool isRoot);

    // navigateTo with the target screen stated. Not Q_INVOKABLE: its callers are
    // goUp(), which reads the kind off the breadcrumb segment, and
    // goToContainingFolder(), which reads it off the resolved path. revealName
    // selects that row once the listing lands, as in refreshVisibleListing.
    void navigateToKind(quint64 handle,
                        bool isRoot,
                        ViewKind kind,
                        const QString& revealName = QString());

    // Navigates to the folder the named node lives in and selects it there. The entry
    // point for the "Go to folder" menu row, which only a cross-folder listing (a
    // search result, the favourites listing) offers -- see MenuAction::GoToFolder.
    Q_INVOKABLE void goToContainingFolder(quint64 handle, QString name);

    // Switches this tab to the favourites listing; the side panel's Favourites row
    // is the caller. Safe to call while already there -- the service re-fetches
    // without pushing, so repeated clicks don't stack the same screen on the back
    // stack.
    Q_INVOKABLE void openFavourites();

    // Same for the Rubbish bin. "Already there" means the bin's own top level only,
    // so this navigates rather than re-fetches when a folder inside it is open.
    Q_INVOKABLE void openRubbish();

    // Same again for the recently-added listing, which behaves exactly as the
    // favourites one does.
    Q_INVOKABLE void openRecents();

    // Whether this tab's screen allows the action with that stable ID right now, for
    // the keyboard shortcuts that stand in for a menu row. The menu itself doesn't
    // need it -- its rows already come from the same resolver.
    Q_INVOKABLE bool canPerform(const QString& actionId) const;

    // Empty query restores the cached folder listing, no round-trip. A non-empty one
    // searches recursively under the open folder; results replace the list but leave
    // navigation state alone, so openFolder() still works from search results.
    Q_INVOKABLE void search(QString query);

    // The advanced-search popup's four facets, as ints so this header stays free of
    // Qt-side enum types (same argument as viewKind above); QML passes
    // SearchNodeType/SearchCategory/SearchTimeWindow values. Out-of-range values fall
    // back to "any". Re-runs the current query, and a filter with no query is itself a
    // search -- clearing both is what restores the cached folder listing.
    Q_INVOKABLE void setSearchFilter(int nodeType,
                                     int category,
                                     int createdWithin,
                                     bool favouritesOnly);

    // column: 0=Name, 1=ModificationTime, 2=Size. Also called at startup with the
    // persisted value, before login/loadRoot() have run (see mHasLoadedOnce).
    Q_INVOKABLE void setSortOrder(int column, bool ascending);

    // Called when auth reaches LoggedOut, so a subsequent login -- possibly a
    // different account -- never shows the previous one's cached listing or reuses
    // its back-stack handles.
    Q_INVOKABLE void reset();

    // Toolbar refresh / F5: syncs with the server first, then re-reads this tab's
    // listing; no-op until the first successful load. The sync is the point --
    // getChildren alone only returns what the SDK was already told, so without it
    // the button guarantees nothing about freshness.
    Q_INVOKABLE void refresh();

    // Re-reads only if this tab shows (handle, isRoot), so a "folder X changed"
    // notification can be fanned out to every tab. Not refresh(): the caller is
    // reporting a change this app just made, which the SDK already knows about, so
    // syncing once per showing tab would be that many pointless round-trips.
    Q_INVOKABLE void refreshIfShowing(quint64 handle, bool isRoot);

    // The two below are TabsController's view of this one, and plain public for
    // the same reason as the four further down: nothing in QML calls them.

    // A favourite toggled in another tab. Not applyFavouriteChange(): that one is
    // this tab's own toggle, which the user is watching, so a favourites listing
    // re-queries at once. Reached from the background, it only marks itself.
    void applyRemoteFavouriteChange(quint64 handle, bool favourite);

    // Re-reads if a fan-out marked this tab stale while it wasn't the one on
    // screen; no-op otherwise. Called when the tab becomes current -- deferring
    // the re-read is what keeps one moved node from costing every favourites tab
    // a full-drive search (FAVOURITES_VIEW_SPEC.md 5.3).
    void refreshIfStale();

    // The four below are FileMutationController's view of this one -- public rather
    // than Q_INVOKABLE on purpose, so nothing in QML binds to them.

    // Re-fetches whatever the user is looking at: the folder listing, or the active
    // search's results plus the cached listing behind them. Mutations go through
    // this so they never silently drop the user out of a search.
    //
    // revealName, when given, selects the row of that name once *this request's*
    // listing is in the model and asks the views to scroll to it -- how a
    // just-created folder gets revealed, since it does not exist in the model
    // when the caller asks. Carried on the request rather than stored, because
    // stored it would outlive requests that never produce a listing (a failed
    // refresh, or the search branch, which drops it) and then fire against an
    // unrelated folder. It also cannot be guarded by comparing the current
    // handle: that comes from mBreadcrumb, which this class updates *after*
    // applying the listing, so it still names the previous folder here.
    void refreshVisibleListing(QString revealName = QString());

    // Writes one node's new favourite flag into both the model and the cached
    // listing, instead of re-reading the folder -- see FileListModel::setFavourite.
    // The cache matters: without it, clearing a search restores the pre-toggle flag.
    // In a favourites listing there is nothing to write: un-favouriting removes the
    // row, so that screen re-fetches instead (FAVOURITES_VIEW_SPEC.md 4.4).
    void applyFavouriteChange(quint64 handle, bool favourite);

    // applyFavouriteChange's counterpart for "this node now has / no longer has a
    // public link". One method for both this tab and the fan-out to the others,
    // where the favourite flag needs two: no screen is *defined* by an export, so
    // nothing has to re-query and nothing marks itself stale.
    void applyExportChange(quint64 handle, bool exported);

    // What the mutation half gates paste on: before the first load there is no
    // folder for it to target.
    bool isLoaded() const;

    // The cached listing, not the server: the mutation half's fallback when a
    // paste's destination re-read fails. Whole entries rather than names because
    // a copy also has to recognise its own sources among them -- pasting into the
    // folder they already live in duplicates rather than collides.
    const std::vector<FileEntry>& cachedChildren() const;

    // Folders only: MEGA lets a file and a folder share a name, so a same-named
    // file is no conflict for createFolder (IMegaClient::createFolder).
    bool hasChildFolderNamed(const std::string& name) const;

signals:
    void canGoBackChanged();
    void breadcrumbChanged();
    void busyChanged();
    void searchActiveChanged();
    void searchFilterChanged();

    // The tab dropped its query and filter because it navigated. The search box owns
    // its text and the filter popup its *pending* edit, so both have to be told;
    // nothing else can reach them.
    void searchCleared();

    // Row is already selected in the model when this fires; the views only have
    // to scroll. Both of a tab's views listen -- the hidden one positions itself
    // so switching view mode doesn't land somewhere else.
    void revealRowRequested(int row);

    // The screen this tab moved to imposed its own order (Recents opens newest
    // first), so the column header has to follow. Deliberately not routed into the
    // window-wide last-write-wins value the views persist: that one is the user's
    // starting point for new tabs, which a screen default must not rewrite.
    void sortOrderReset(int column, bool ascending);

private:
    void applyResult(Result<std::vector<FileEntry>> result, const QString& revealName = QString());
    void applySearchResult(Result<std::vector<FileEntry>> result);

    // Shared tail of search() and setSearchFilter(): publishes searchActive if it
    // flipped, then either restores the cached listing or runs the search.
    void applySearchCriteria(bool wasActive);

    // Drops the query because the tab is moving somewhere the results do not
    // describe. Called by the navigation entry points and by none of the refresh
    // ones -- a refresh stays inside the search, which is what
    // refreshVisibleListing() exists to preserve.
    void dropSearchForNavigation();

    // Re-runs mLastSearchQuery against whatever this tab is showing. In a favourites
    // listing the search box narrows the favourites rather than searching a folder,
    // so SearchService -- which is defined as "recursive search under the current
    // folder" -- is bypassed instead of taught about view kinds.
    void runVisibleSearch();

    void refreshCurrentFolder(QString revealName = QString());

    // refreshVisibleListing() behind the mHasLoadedOnce guard.
    void refreshListingIfLoaded();

    // Writes one node's flag into the model and the cached listing. The folder
    // half of both favourite entry points.
    void writeFavouriteFlag(quint64 handle, bool favourite);

    // Records that what this tab shows no longer matches the account, for
    // refreshIfStale() to act on later.
    void markStale();

    // Installs the order the screen of that ViewKind imposes on itself and tells the
    // header about it. No-op while the tab is already on that screen, so re-clicking
    // the side panel's row doesn't throw away a sort the user picked there.
    void applyViewSortOrder(int kind, SortOrder order);

    // Undoes applyViewSortOrder() when the tab navigates away, so a screen's own
    // default doesn't follow the user into the next folder. Called by every
    // navigation entry point except the one that imposes an order.
    void restoreUserSortOrder();

    // Resolves the current location's ancestor chain, so the breadcrumb tracks the
    // folder hierarchy rather than navigation history. Emits breadcrumbChanged only
    // when the resolved path differs, so a sort-order change doesn't rebuild the
    // Breadcrumb Repeater.
    void refreshBreadcrumb();

    // Copies the breadcrumb-derived view kind into the model, which needs it to
    // resolve availableActions. Called from the only two places mBreadcrumb changes.
    void publishViewKind();

    // The other half of what availableActions resolves against. Separate from
    // publishViewKind because the search box moves it without the location changing.
    void publishCrossFolderListing();


    std::shared_ptr<FolderNavigationService> mService;
    std::shared_ptr<SearchService> mSearchService;
    NotificationController* mNotifications;
    std::shared_ptr<FileListModel> mFileListModel;
    QVariantList mBreadcrumb;
    std::vector<FileEntry> mLastFolderEntries; // restored when search is cleared
    SortOrder mSortOrder{SortKey::Name, true};
    std::string mLastSearchQuery; // empty == no name predicate
    // The advanced-search facets. Together with mLastSearchQuery this is the search:
    // either being non-default makes searchActive() true.
    SearchFilter mSearchFilter;
    // Whether the rows in the model are search hits rather than one folder's
    // children. Not derivable from mLastSearchQuery: opening a folder from the
    // results replaces the rows but deliberately leaves the query alone.
    bool mListingFromSearch = false;
    // QML's Component.onCompleted restores the persisted sort before login has ever
    // run; this guards that startup setSortOrder() from re-fetching and erroring.
    bool mHasLoadedOnce = false;
    // A screen imposed its own order (applyViewSortOrder) before that startup
    // restore arrived. A new tab's FileTableView completes and openRecents() runs
    // in an order QML gives no guarantee about, so without this the restored
    // window-wide order can land last and undo the screen's default.
    bool mSortOrderSetByView = false;
    // The order that was in effect before a screen imposed its own, put back when
    // the tab leaves that screen. Unset once the user picks an order themselves.
    std::optional<SortOrder> mSortOrderBeforeView = std::nullopt;
    // Set by a fan-out this tab couldn't apply in place; cleared by the re-read.
    bool mStale = false;
    // Publishes busy() and owns the delay before a spinner appears. Shared because
    // the mutation half writes it too.
    std::shared_ptr<BusyState> mBusy;
};
