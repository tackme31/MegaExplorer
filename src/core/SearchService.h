#pragma once
#include "FolderNavigationService.h"
#include "IMegaClient.h"

#include <memory>

// Recursive name search scoped to wherever FolderNavigationService currently
// is. SDK-free by construction (depends only on IMegaClient and
// FolderNavigationService, both SDK-free themselves), unit-testable with a
// mocked IMegaClient like the other core services.
class SearchService
{
public:
    explicit SearchService(std::shared_ptr<IMegaClient> client,
                           std::shared_ptr<FolderNavigationService> navigationService);

    // Empty query fails without calling IMegaClient::search: callers (the
    // QML controller) are expected to treat an empty query as "clear search"
    // and never reach this method for that case, but this guard keeps the
    // service safe to call directly too.
    void search(const std::string& query,
                SortOrder order,
                std::function<void(Result<std::vector<FileEntry>>)> onDone);

private:
    std::shared_ptr<IMegaClient> mClient;
    std::shared_ptr<FolderNavigationService> mNavigationService;
};
