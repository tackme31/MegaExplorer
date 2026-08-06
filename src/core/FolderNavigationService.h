#pragma once
#include "IMegaClient.h"

#include <memory>
#include <vector>

// Post-login folder navigation over an already-fetched node tree. Requires
// AuthService::login/restoreSession (via its internal fetchNodes step) to
// have already succeeded once (same precondition as
// IMegaClient::getChildren/getRootChildren themselves). SDK-free by
// construction (depends only on IMegaClient), unit-testable with a mock of
// it like AuthService.
//
// IMegaClient has no handle for "root" (getRootChildren takes none), so
// Location::isRoot is used as the "currently at root" / "back-stack entry is
// root" sentinel throughout.
//
// This class carries no mutex, unlike DownloadService/UploadService/
// ThumbnailService, and that is a borrowed guarantee rather than a local one:
// getRootChildren/getChildren/search are delivery mode 2 in IMegaClient.h --
// always synchronous, on the calling thread -- so mCurrent and the back-stack
// are only ever touched from the GUI thread. If any of those three ever
// starts answering from an SDK thread, this class needs one.
//
// openRoot/openFolder/goBack/refreshCurrent all share the same
// single-callback shape: onDone fires exactly once with the authoritative
// IMegaClient result.
class FolderNavigationService
{
public:
    explicit FolderNavigationService(std::shared_ptr<IMegaClient> client);

    // Fetches the root's children via IMegaClient::getRootChildren. Never
    // touches the back-stack or mCurrent -- root is the permanent "home",
    // not a location that gets pushed/popped.
    void openRoot(SortOrder order, std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // Fetches handle's children via IMegaClient::getChildren. On success,
    // pushes the previous current location (handle or root sentinel) onto
    // the back-stack and makes handle the new current location. On failure,
    // state is unchanged.
    void openFolder(std::uint64_t handle,
                    SortOrder order,
                    std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // Navigates to an arbitrary location, pushing the previous current
    // location onto the back-stack (Explorer semantics: a breadcrumb click
    // is a navigation, Back returns to where you were). Generalizes
    // openFolder to also cover the root, which openRoot deliberately can't
    // do -- openRoot is the initial "home" load and never touches history.
    void navigateTo(std::uint64_t handle,
                    bool isRoot,
                    SortOrder order,
                    std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // Peeks the most recent back-stack entry and re-fetches it (getChildren
    // if it was a real handle, getRootChildren if it was the root sentinel).
    // Only pops/commits the new current location on a successful re-fetch.
    // Fails immediately without fetching if canGoBack() is false.
    void goBack(SortOrder order, std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // Re-fetches the current location (mCurrent) with a new order, without
    // touching the back-stack or mCurrent itself -- used when the user
    // changes the sort column/direction while staying in the same folder
    // (see FolderNavigationController::setSortOrder).
    void refreshCurrent(SortOrder order,
                        std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // Reads an arbitrary folder's children without going there: no back-stack
    // push, no mCurrent change, nothing observable afterwards. refreshCurrent
    // above can't serve this -- a drag-copy's destination is whatever folder
    // the pointer was over, which is usually not the one this tab is showing,
    // and the copy has to know that folder's existing names before it picks
    // any (IMegaClient::copyNode).
    void listChildrenOf(std::uint64_t handle,
                        bool isRoot,
                        SortOrder order,
                        std::function<void(Result<std::vector<FileEntry>>)> onDone);

    bool canGoBack() const;

    // Clears the back-stack and returns to the root sentinel, without
    // touching IMegaClient. Used after logout (see
    // FolderNavigationController::reset) so a subsequent login -- possibly a
    // different account -- doesn't retain back-stack handles that belong to
    // the previous session's node tree.
    void resetToRoot();

    // Public mirror of the private Location below, for callers (e.g. search)
    // that need to scope an operation to "wherever the user currently is"
    // without exposing Location itself.
    struct CurrentLocation
    {
        bool isRoot;
        std::uint64_t handle;
    };
    CurrentLocation currentLocation() const;

    // Resolves the ancestor chain of the current location for the breadcrumb.
    // Read-only: never touches the back-stack or mCurrent.
    void resolveCurrentPath(std::function<void(Result<std::vector<PathSegment>>)> onDone);

    // Straight passthrough to IMegaClient::syncPendingChanges, and location-
    // agnostic despite living here: this service is the only one of the three
    // FolderNavigationController holds that owns an IMegaClient, and a
    // user-initiated refresh is the only caller. Routing it through here
    // rather than injecting a fourth dependency into that controller.
    void syncWithServer(std::function<void(Result<void>)> onDone);

private:
    struct Location
    {
        bool isRoot = true;
        std::uint64_t handle = 0;
    };

    // Shared "network call -> commit state on success -> onDone" sequence
    // behind openRoot/openFolder/goBack.
    //   network: the IMegaClient call to make, already bound to
    //     handle/order -- takes just the completion callback.
    //   onCommit: state mutation to run only on network success (e.g.
    //     openFolder's back-stack push, goBack's pop) -- kept separate so
    //     this helper stays agnostic of which entry point called it.
    void
    runAndCommit(std::function<void(std::function<void(Result<std::vector<FileEntry>>)>)> network,
                 std::function<void()> onCommit,
                 std::function<void(Result<std::vector<FileEntry>>)> onDone);

    std::shared_ptr<IMegaClient> mClient;
    std::vector<Location> mBackStack;
    Location mCurrent; // isRoot == true == currently at root (initial state)
};
