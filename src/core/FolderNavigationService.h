#pragma once
#include "IMegaClient.h"
#include <memory>
#include <vector>

// Post-login folder navigation over an already-fetched node tree. Requires
// FileListingService::loadRootListing to have already succeeded once (same
// precondition as IMegaClient::getChildren/getRootChildren themselves).
// SDK-free by construction (depends only on IMegaClient), unit-testable with
// a mocked IMegaClient like FileListingService.
//
// IMegaClient has no handle for "root" (getRootChildren takes none), so
// Location::isRoot is used as the "currently at root" / "back-stack entry is
// root" sentinel throughout. Not std::optional<uint64_t>: this project's
// CMAKE_CXX_STANDARD isn't pinned to C++17+ (see Result.h's own comment on
// avoiding std::expected), so std::optional can't be assumed available.
class FolderNavigationService
{
public:
    explicit FolderNavigationService(std::shared_ptr<IMegaClient> client);

    // Fetches handle's children via IMegaClient::getChildren. On success,
    // pushes the previous current location (handle or root sentinel) onto
    // the back-stack and makes handle the new current location. On failure,
    // state is unchanged.
    void openFolder(std::uint64_t handle,
                     std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // Peeks the most recent back-stack entry and re-fetches it (getChildren
    // if it was a real handle, getRootChildren if it was the root sentinel).
    // Only pops/commits the new current location on a successful re-fetch.
    // Fails immediately without fetching if canGoBack() is false.
    void goBack(std::function<void(Result<std::vector<FileEntry>>)> onDone);

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

    std::shared_ptr<IMegaClient> mClient;
    std::vector<Location> mBackStack;
    Location mCurrent; // isRoot == true == currently at root (initial state)
};
