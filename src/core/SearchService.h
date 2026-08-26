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

    // An empty query with a filter that narrows nothing fails without reaching the
    // SDK -- that pair would list the whole subtree, which is not what "search" means
    // here. A filter on its own is a search, so only the pair is refused.
    //
    // Unlike FolderNavigationService's own listings this has no generation token, so
    // a result outliving the screen that asked for it is the caller's to drop.
    void search(const std::string& query,
                const SearchFilter& filter,
                SortOrder order,
                std::function<void(Result<std::vector<FileEntry>>)> onDone);

private:
    std::shared_ptr<IMegaClient> mClient;
    std::shared_ptr<FolderNavigationService> mNavigationService;
};
