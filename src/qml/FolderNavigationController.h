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

    // Not Q_INVOKABLE: QML reaches the root load through
    // TabsController::loadRootAll(), never this per-tab entry point.
    void loadRoot();

    Q_INVOKABLE void openFolder(quint64 handle);
    Q_INVOKABLE void goBack();

    // Routed through navigateTo, so "up" lands on the back-stack and "back" undoes
    // it, as in Explorer.
    Q_INVOKABLE void goUp();

    Q_INVOKABLE void navigateTo(quint64 handle, bool isRoot);

    // Switches this tab to the favourites listing; the side panel's Favourites row
    // is the caller. Safe to call while already there -- the service re-fetches
    // without pushing, so repeated clicks don't stack the same screen on the back
    // stack.
    Q_INVOKABLE void openFavourites();

    // Whether this tab's screen allows the action with that stable ID right now, for
    // the keyboard shortcuts that stand in for a menu row. The menu itself doesn't
    // need it -- its rows already come from the same resolver.
    Q_INVOKABLE bool canPerform(const QString& actionId) const;

    // Empty query restores the cached folder listing, no round-trip. A non-empty one
    // searches recursively under the open folder; results replace the list but leave
    // navigation state alone, so openFolder() still works from search results.
    Q_INVOKABLE void search(QString query);

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

    // The four below are FileMutationController's view of this one -- public rather
    // than Q_INVOKABLE on purpose, so nothing in QML binds to them.

    // Re-fetches whatever the user is looking at: the folder listing, or the active
    // search's results plus the cached listing behind them. Mutations go through
    // this so they never silently drop the user out of a search.
    void refreshVisibleListing();

    // Writes one node's new favourite flag into both the model and the cached
    // listing, instead of re-reading the folder -- see FileListModel::setFavourite.
    // The cache matters: without it, clearing a search restores the pre-toggle flag.
    void applyFavouriteChange(quint64 handle, bool favourite);

    // What the mutation half gates paste on: before the first load there is no
    // folder for it to target.
    bool isLoaded() const;

    // Child names from the cached listing, not the server: the mutation half's
    // fallback when a paste's destination re-read fails.
    std::set<std::string> cachedChildNames() const;

signals:
    void canGoBackChanged();
    void breadcrumbChanged();
    void busyChanged();

private:
    void applyResult(Result<std::vector<FileEntry>> result);
    void applySearchResult(Result<std::vector<FileEntry>> result);

    // Re-runs mLastSearchQuery against whatever this tab is showing. In a favourites
    // listing the search box narrows the favourites rather than searching a folder,
    // so SearchService -- which is defined as "recursive search under the current
    // folder" -- is bypassed instead of taught about view kinds.
    void runVisibleSearch();

    void refreshCurrentFolder();

    // refreshVisibleListing() behind the mHasLoadedOnce guard.
    void refreshListingIfLoaded();

    // Resolves the current location's ancestor chain, so the breadcrumb tracks the
    // folder hierarchy rather than navigation history. Emits breadcrumbChanged only
    // when the resolved path differs, so a sort-order change doesn't rebuild the
    // Breadcrumb Repeater.
    void refreshBreadcrumb();

    // Copies the breadcrumb-derived view kind into the model, which needs it to
    // resolve availableActions. Called from the only two places mBreadcrumb changes.
    void publishViewKind();

    std::shared_ptr<FolderNavigationService> mService;
    std::shared_ptr<SearchService> mSearchService;
    NotificationController* mNotifications;
    std::shared_ptr<FileListModel> mFileListModel;
    QVariantList mBreadcrumb;
    std::vector<FileEntry> mLastFolderEntries; // restored when search is cleared
    SortOrder mSortOrder{SortKey::Name, true};
    std::string mLastSearchQuery; // empty == not currently searching
    // QML's Component.onCompleted restores the persisted sort before login has ever
    // run; this guards that startup setSortOrder() from re-fetching and erroring.
    bool mHasLoadedOnce = false;
    // Publishes busy() and owns the delay before a spinner appears. Shared because
    // the mutation half writes it too.
    std::shared_ptr<BusyState> mBusy;
};
