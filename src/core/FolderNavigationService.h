#pragma once
#include "IMegaClient.h"
#include "INodeCache.h"

#include <memory>
#include <vector>

// Post-login folder navigation over an already-fetched node tree. Requires
// FileListingService::loadRootListing to have already succeeded once (same
// precondition as IMegaClient::getChildren/getRootChildren themselves).
// SDK-free by construction (depends only on IMegaClient/INodeCache),
// unit-testable with mocks of both like FileListingService.
//
// IMegaClient has no handle for "root" (getRootChildren takes none), so
// Location::isRoot is used as the "currently at root" / "back-stack entry is
// root" sentinel throughout. Not std::optional<uint64_t>: this project's
// CMAKE_CXX_STANDARD isn't pinned to C++17+ (see Result.h's own comment on
// avoiding std::expected), so std::optional can't be assumed available.
//
// Cache-then-refresh (Phase 6): openRoot/openFolder/goBack each take two
// callbacks. onCacheHit fires synchronously, at most once, only when
// INodeCache has a non-empty cached row set for the folder being opened --
// it's a display hint, never an error. onRefreshed always fires exactly
// once with the authoritative IMegaClient result, which -- on success -- is
// also written back into INodeCache for next time. refreshCurrent (used
// only for in-place sort-order changes on an already-displayed folder) is
// deliberately not part of this: the caller already has something on
// screen, so there's nothing useful for a cache read to add. It still
// writes its result through to the cache so a later openFolder/openRoot of
// the same folder stays warm.
class FolderNavigationService
{
public:
    explicit FolderNavigationService(std::shared_ptr<IMegaClient> client,
                                     std::shared_ptr<INodeCache> cache);

    // Fetches the root's children via IMegaClient::getRootChildren. Never
    // touches the back-stack or mCurrent -- root is the permanent "home",
    // not a location that gets pushed/popped.
    void openRoot(SortOrder order,
                  std::function<void(std::vector<FileEntry>)> onCacheHit,
                  std::function<void(Result<std::vector<FileEntry>>)> onRefreshed);

    // Fetches handle's children via IMegaClient::getChildren. On success,
    // pushes the previous current location (handle or root sentinel) onto
    // the back-stack and makes handle the new current location. On failure,
    // state is unchanged.
    void openFolder(std::uint64_t handle,
                    SortOrder order,
                    std::function<void(std::vector<FileEntry>)> onCacheHit,
                    std::function<void(Result<std::vector<FileEntry>>)> onRefreshed);

    // Peeks the most recent back-stack entry and re-fetches it (getChildren
    // if it was a real handle, getRootChildren if it was the root sentinel).
    // Only pops/commits the new current location on a successful re-fetch.
    // Fails immediately without fetching -- and without a cache lookup --
    // if canGoBack() is false.
    void goBack(SortOrder order,
                std::function<void(std::vector<FileEntry>)> onCacheHit,
                std::function<void(Result<std::vector<FileEntry>>)> onRefreshed);

    // Re-fetches the current location (mCurrent) with a new order, without
    // touching the back-stack or mCurrent itself -- used when the user
    // changes the sort column/direction while staying in the same folder
    // (see FolderNavigationController::setSortOrder).
    void refreshCurrent(SortOrder order,
                        std::function<void(Result<std::vector<FileEntry>>)> onDone);

    bool canGoBack() const;

    // Public mirror of the private Location below, for callers (e.g. search)
    // that need to scope an operation to "wherever the user currently is"
    // without exposing Location itself.
    struct CurrentLocation
    {
        bool isRoot;
        std::uint64_t handle;
    };
    CurrentLocation currentLocation() const;

private:
    struct Location
    {
        bool isRoot = true;
        std::uint64_t handle = 0;
    };

    // Shared "cache lookup -> network call -> commit state -> write back ->
    // onRefreshed" sequence behind openRoot/openFolder/goBack.
    //   key: the INodeCache::ParentKey to look up/write back under.
    //   network: the IMegaClient call to make, already bound to
    //     handle/order -- takes just the completion callback.
    //   onCommit: state mutation to run only on network success (e.g.
    //     openFolder's back-stack push, goBack's pop) -- kept separate so
    //     this helper stays agnostic of which entry point called it.
    void
    loadWithCache(const INodeCache::ParentKey& key,
                  std::function<void(std::function<void(Result<std::vector<FileEntry>>)>)> network,
                  std::function<void()> onCommit,
                  std::function<void(std::vector<FileEntry>)> onCacheHit,
                  std::function<void(Result<std::vector<FileEntry>>)> onRefreshed);

    std::shared_ptr<IMegaClient> mClient;
    std::shared_ptr<INodeCache> mCache;
    std::vector<Location> mBackStack;
    Location mCurrent; // isRoot == true == currently at root (initial state)
};
