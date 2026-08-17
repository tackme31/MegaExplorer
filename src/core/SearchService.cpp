#include "SearchService.h"

#include "MegaErrorCodes.h"

SearchService::SearchService(std::shared_ptr<IMegaClient> client,
                             std::shared_ptr<FolderNavigationService> navigationService)
    : mClient(std::move(client)), mNavigationService(std::move(navigationService))
{}

void SearchService::search(const std::string& query,
                           const SearchFilter& filter,
                           SortOrder order,
                           std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    if (query.empty() && filter.isDefault())
    {
        onDone(Result<std::vector<FileEntry>>::fail("empty query", MegaErrorCode::kEArgs));
        return;
    }

    FolderNavigationService::CurrentLocation location = mNavigationService->currentLocation();
    mClient->search(location.handle, location.isRoot, query, filter, order, std::move(onDone));
}
