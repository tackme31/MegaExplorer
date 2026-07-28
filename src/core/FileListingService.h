#pragma once
#include "FolderNavigationService.h"
#include "IMegaClient.h"

#include <memory>

// Orchestrates login -> fetchNodes -> FolderNavigationService::openRoot over
// an IMegaClient. SDK-free by construction (depends only on IMegaClient and
// FolderNavigationService, both SDK-free themselves -- same pattern
// SearchService already uses), so it can be unit tested with a mocked
// IMegaClient/INodeCache pair.
//
// The final listing step is delegated to FolderNavigationService::openRoot
// (rather than calling IMegaClient::getRootChildren directly, as before
// Phase 6) so the root listing gets the same cache-then-refresh treatment
// as any other folder -- the root is what's on screen right after the
// slowest part of startup (login+fetchNodes), so it's the highest-value
// place for a cached flash of content.
class FileListingService
{
public:
    explicit FileListingService(std::shared_ptr<IMegaClient> client,
                                std::shared_ptr<FolderNavigationService> navigationService);

    // onCacheHit fires synchronously, at most once, only if openRoot finds a
    // non-empty cached root listing -- never on a login/fetchNodes failure
    // (those short-circuit straight to onRefreshed, exactly as before).
    // onRefreshed always fires exactly once, with the login/fetchNodes
    // failure or the authoritative root listing.
    void loadRootListing(const std::string& email,
                         const std::string& password,
                         SortOrder order,
                         std::function<void(std::vector<FileEntry>)> onCacheHit,
                         std::function<void(Result<std::vector<FileEntry>>)> onRefreshed);

private:
    std::shared_ptr<IMegaClient> mClient;
    std::shared_ptr<FolderNavigationService> mNavigationService;
};
