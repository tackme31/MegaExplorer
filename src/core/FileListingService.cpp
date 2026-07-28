#include "FileListingService.h"

FileListingService::FileListingService(std::shared_ptr<IMegaClient> client,
                                       std::shared_ptr<FolderNavigationService> navigationService)
    : mClient(std::move(client)), mNavigationService(std::move(navigationService))
{}

void FileListingService::loadRootListing(
    const std::string& email,
    const std::string& password,
    SortOrder order,
    std::function<void(std::vector<FileEntry>)> onCacheHit,
    std::function<void(Result<std::vector<FileEntry>>)> onRefreshed)
{
    mClient->login(
        email, password, [this, order, onCacheHit, onRefreshed](Result<void> loginResult) {
            if (!loginResult.success)
            {
                onRefreshed(Result<std::vector<FileEntry>>::fail(loginResult.errorMessage,
                                                                 loginResult.errorCode));
                return;
            }

            mClient->fetchNodes([this, order, onCacheHit, onRefreshed](Result<void> fetchResult) {
                if (!fetchResult.success)
                {
                    onRefreshed(Result<std::vector<FileEntry>>::fail(fetchResult.errorMessage,
                                                                     fetchResult.errorCode));
                    return;
                }

                mNavigationService->openRoot(order, onCacheHit, onRefreshed);
            });
        });
}
