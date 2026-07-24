#include "FileListingService.h"

FileListingService::FileListingService(std::shared_ptr<IMegaClient> client)
    : mClient(std::move(client))
{
}

void FileListingService::loadRootListing(
    const std::string& email,
    const std::string& password,
    std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    mClient->login(email, password, [this, onDone](Result<void> loginResult) {
        if (!loginResult.success)
        {
            onDone(Result<std::vector<FileEntry>>::fail(loginResult.errorMessage,
                                                          loginResult.errorCode));
            return;
        }

        mClient->fetchNodes([this, onDone](Result<void> fetchResult) {
            if (!fetchResult.success)
            {
                onDone(Result<std::vector<FileEntry>>::fail(fetchResult.errorMessage,
                                                              fetchResult.errorCode));
                return;
            }

            mClient->getRootChildren(onDone);
        });
    });
}
