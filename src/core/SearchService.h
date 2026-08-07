#pragma once
#include "FolderNavigationService.h"
#include "IMegaClient.h"

#include <memory>

// Recursive name search scoped to wherever FolderNavigationService currently is.
class SearchService
{
public:
    explicit SearchService(std::shared_ptr<IMegaClient> client,
                           std::shared_ptr<FolderNavigationService> navigationService);

    // An empty query fails without reaching the SDK. Callers treat that as "clear
    // search" and never get here, but the guard keeps direct calls safe.
    void search(const std::string& query,
                SortOrder order,
                std::function<void(Result<std::vector<FileEntry>>)> onDone);

private:
    std::shared_ptr<IMegaClient> mClient;
    std::shared_ptr<FolderNavigationService> mNavigationService;
};
