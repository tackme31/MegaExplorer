#include "FileListingService.h"

FileListingService::FileListingService(std::shared_ptr<IMegaClient> client)
    : mClient(std::move(client))
{}

void FileListingService::loadRootListing(const std::string& email,
                                         const std::string& password,
                                         SortOrder order,
                                         std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    mClient->login(email, password, [this, order, onDone](Result<void> loginResult) {
        if (!loginResult.success)
        {
            onDone(Result<std::vector<FileEntry>>::fail(loginResult.errorMessage,
                                                        loginResult.errorCode));
            return;
        }

        mClient->fetchNodes([this, order, onDone](Result<void> fetchResult) {
            if (!fetchResult.success)
            {
                onDone(Result<std::vector<FileEntry>>::fail(fetchResult.errorMessage,
                                                            fetchResult.errorCode));
                return;
            }

            mClient->getRootChildren(order, onDone);
        });
    });
}
