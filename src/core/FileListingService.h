#pragma once
#include "IMegaClient.h"

#include <memory>

// Orchestrates login -> fetchNodes -> getRootChildren over an IMegaClient.
// SDK-free by construction (depends only on IMegaClient), so it can be unit
// tested with a mocked IMegaClient.
class FileListingService
{
public:
    explicit FileListingService(std::shared_ptr<IMegaClient> client);

    void loadRootListing(const std::string& email,
                         const std::string& password,
                         SortOrder order,
                         std::function<void(Result<std::vector<FileEntry>>)> onDone);

private:
    std::shared_ptr<IMegaClient> mClient;
};
