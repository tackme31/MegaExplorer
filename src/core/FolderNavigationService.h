#pragma once
#include "IMegaClient.h"

#include <memory>
#include <vector>

// Post-login folder navigation over an already-fetched node tree: a successful
// fetchNodes is a precondition throughout.
//
// This class carries no mutex, unlike DownloadService/UploadService/
// ThumbnailService, and that is a borrowed guarantee: getRootChildren/getChildren/
// search always answer synchronously on the calling thread, so mCurrent and the
// back-stack are only ever touched from the GUI thread. If any of those three ever
// starts answering from an SDK thread, this class needs one.
class FolderNavigationService
{
public:
    explicit FolderNavigationService(std::shared_ptr<IMegaClient> client);

    // Never touches the back-stack or mCurrent: root is the permanent "home", not a
    // location that gets pushed and popped.
    void openRoot(SortOrder order, std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // Pushes the previous location onto the back-stack on success only; a failure
    // leaves all state unchanged.
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

    // Peeks the most recent back-stack entry and re-fetches it, popping only on
    // success. Fails in-stack when canGoBack() is false.
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
    void listFavourites(SortOrder order,
                        const std::string& nameFilter,
                        std::function<void(Result<std::vector<FileEntry>>)> onDone);

    bool canGoBack() const;

    // Clears the back-stack without touching IMegaClient, so a login after logout
    // can't retain handles belonging to the previous session's node tree.
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
    // has. Both are meaningless for Favourites, which is a flat listing with no
    // node behind it; for Rubbish, isRoot means the bin's own top level and a
    // handle names a folder inside it.
    struct Location
    {
        ViewKind kind = ViewKind::CloudDrive;
        bool isRoot = true;
        std::uint64_t handle = 0;
    };

    // Shared "call -> commit state on success -> onDone" sequence. network is the
    // IMegaClient call already bound to handle/order; onCommit runs only on success
    // and is kept separate so this stays agnostic of which entry point called it.
    void
    runAndCommit(std::function<void(std::function<void(Result<std::vector<FileEntry>>)>)> network,
                 std::function<void()> onCommit,
                 std::function<void(Result<std::vector<FileEntry>>)> onDone);

    std::shared_ptr<IMegaClient> mClient;
    std::vector<Location> mBackStack;
    Location mCurrent; // isRoot == true == currently at root (initial state)
};
