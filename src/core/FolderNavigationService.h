#pragma once
#include "IMegaClient.h"

#include <memory>
#include <vector>

// Post-login folder navigation over an already-fetched node tree: a successful
// fetchNodes is a precondition throughout.
//
// This class carries no mutex, unlike DownloadService/UploadService/
// ThumbnailService, and that is a borrowed guarantee: every listing call it makes
// answers on the calling thread, so mCurrent, the back-stack and the navigation
// counter are only ever touched from the GUI thread. getRootChildren/getChildren/
// search answer there synchronously; listFavourites/listRecent answer there a turn
// later, which is the same guarantee and not a weaker one. If any of them ever
// starts answering from an SDK thread, this class needs a mutex.
class FolderNavigationService
{
public:
    explicit FolderNavigationService(std::shared_ptr<IMegaClient> client);

    // Never touches the back-stack or mCurrent: root is the permanent "home", not a
    // location that gets pushed and popped.
    void openRoot(SortOrder order, std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // Pushes the previous location onto the back-stack before the fetch is issued, so
    // the move is visible at click time; a failure rolls that back, leaving all state
    // unchanged.
    void openFolder(std::uint64_t handle,
                    SortOrder order,
                    std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // openFolder generalized to also cover the root, which openRoot deliberately
    // can't: a breadcrumb click is a navigation, so Back returns to where you were.
    // kind names which screen the target belongs to, and must be supplied rather
    // than derived: isRoot alone no longer identifies a root now that the Rubbish
    // bin has a top of its own, so "up" out of a binned folder and a click on the
    // tree's Cloud Drive row would otherwise be indistinguishable.
    void navigateTo(std::uint64_t handle,
                    bool isRoot,
                    ViewKind kind,
                    SortOrder order,
                    std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // Pushes the current location and switches this tab to the favourites listing,
    // which is a location in its own right rather than a folder. Calling it while
    // already there re-fetches without pushing, so repeated clicks on the same
    // side-panel row can't stack the same screen up for Back to walk down again.
    void openFavourites(SortOrder order,
                        std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // openFavourites' counterpart for the Rubbish bin. Unlike favourites this screen
    // is a real subtree, so openFolder() from here stays inside it -- see navigateTo.
    void openRubbish(SortOrder order, std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // openFavourites' counterpart for the recently-added listing, which is the same
    // shape of screen: a flat cross-drive query, not a folder.
    void openRecents(SortOrder order, std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // Pops the most recent back-stack entry and re-fetches it, restoring the entry on
    // failure. Fails in-stack when canGoBack() is false.
    void goBack(SortOrder order, std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // Re-fetches the current location with a new order, touching no state -- the
    // sort-order change path.
    void refreshCurrent(SortOrder order,
                        std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // Reads an arbitrary folder's children without going there. refreshCurrent can't
    // serve this: a drag-copy's destination is whatever folder the pointer was over,
    // usually not the one this tab is showing, and the copy has to know that
    // folder's existing names before picking any (IMegaClient::copyNode).
    void listChildrenOf(std::uint64_t handle,
                        bool isRoot,
                        SortOrder order,
                        std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // listChildrenOf's counterpart for the favourites listing: reads it without
    // going there. The name filter is what makes the search box narrow a favourites
    // listing instead of searching a folder (FAVOURITES_VIEW_SPEC.md 3.5).
    //
    // Claims no screen, but is still dropped when one was claimed while it was out:
    // the listing behind it answers a turn late, so a search typed just before the
    // user left could otherwise repaint the screen they went to.
    void listFavourites(SortOrder order,
                        const std::string& nameFilter,
                        const SearchFilter& filter,
                        std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // listFavourites' counterpart for the recently-added listing.
    void listRecent(SortOrder order,
                    const std::string& nameFilter,
                    const SearchFilter& filter,
                    std::function<void(Result<std::vector<FileEntry>>)> onDone);

    bool canGoBack() const;

    // Clears the back-stack without touching IMegaClient, so a login after logout
    // can't retain handles belonging to the previous session's node tree. Any fetch
    // still in flight is dropped with it.
    void resetToRoot();

    // Public mirror of the private Location, for callers that scope an operation to
    // "wherever the user currently is".
    struct CurrentLocation
    {
        ViewKind kind;
        bool isRoot;
        std::uint64_t handle;
    };
    CurrentLocation currentLocation() const;

    // Resolves the ancestor chain of the current location for the breadcrumb.
    // Read-only: never touches the back-stack or mCurrent.
    void resolveCurrentPath(std::function<void(Result<std::vector<PathSegment>>)> onDone);

    // The ancestor chain of any node, not just the current location: what "go to the
    // folder this item is in" reads the parent off. Passthrough for the same reason
    // as syncWithServer below.
    void resolvePathOf(std::uint64_t handle,
                       std::function<void(Result<std::vector<PathSegment>>)> onDone);

    // Location-agnostic passthrough, here only because this is the one service its
    // controller holds that owns an IMegaClient -- cheaper than a fourth dependency.
    void syncWithServer(std::function<void(Result<void>)> onDone);

private:
    // handle is meaningless while isRoot, the same sentinel nesting isRoot already
    // has. Both are meaningless for Favourites and Recents, which are flat listings
    // with no node behind them; for Rubbish, isRoot means the bin's own top level
    // and a handle names a folder inside it.
    struct Location
    {
        ViewKind kind = ViewKind::CloudDrive;
        bool isRoot = true;
        std::uint64_t handle = 0;
    };

    // Which IMegaClient call reads a given location's listing. Shared by
    // refreshCurrent() and goBack(), which would otherwise carry the same
    // screen-kind chain twice and drift apart as screens are added.
    void fetchListing(const Location& location,
                      SortOrder order,
                      std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // Shared "commit the location -> call -> undo on failure" sequence. onCommit runs
    // before network, so the breadcrumb and Back move at click time rather than when
    // the listing lands; network is the IMegaClient call already bound to
    // handle/order, and stays a parameter so this remains agnostic of the caller.
    // onCommit may be empty for a re-fetch that changes no location.
    //
    // A result that arrives after a later navigation has committed is dropped whole:
    // neither the rollback nor onDone runs, because both would speak for a screen the
    // tab has already left. Per-request bookkeeping added around onDone must
    // therefore not assume it is always reached.
    void
    commitThenRun(std::function<void()> onCommit,
                  std::function<void(std::function<void(Result<std::vector<FileEntry>>)>)> network,
                  std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // commitThenRun's guard without its commit, for a read that re-reads the screen
    // the user is already on: the result is dropped once a later call has claimed a
    // screen, and the generation is deliberately not bumped -- a search is not a
    // navigation, and bumping it here would discard the navigation's own fetch.
    std::function<void(Result<std::vector<FileEntry>>)>
    dropIfScreenChanged(std::function<void(Result<std::vector<FileEntry>>)> onDone);

    std::shared_ptr<IMegaClient> mClient;
    std::vector<Location> mBackStack;
    Location mCurrent; // isRoot == true == currently at root (initial state)

    // Bumped by every call that claims the current screen; an in-flight fetch holds
    // the value it was issued under and is stale once they differ.
    std::uint64_t mGeneration = 0;
};
