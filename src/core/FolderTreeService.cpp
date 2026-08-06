#include "FolderTreeService.h"

FolderTreeService::FolderTreeService(std::shared_ptr<IMegaClient> client)
    : mClient(std::move(client))
{}

void FolderTreeService::loadSubfolders(std::uint64_t handle,
                                       bool isRoot,
                                       std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    auto filterFolders = [onDone = std::move(onDone)](Result<std::vector<FileEntry>> result) {
        if (!result.success)
        {
            onDone(std::move(result));
            return;
        }

        std::vector<FileEntry> folders;
        folders.reserve(result.value().size());
        for (FileEntry& entry : result.value())
        {
            if (entry.isFolder)
                folders.push_back(std::move(entry));
        }
        onDone(Result<std::vector<FileEntry>>::ok(std::move(folders)));
    };

    const SortOrder order{SortKey::Name, true};
    if (isRoot)
        mClient->getRootChildren(order, std::move(filterFolders));
    else
        mClient->getChildren(handle, order, std::move(filterFolders));
}

bool FolderTreeService::hasSubfolders(std::uint64_t handle, bool isRoot) const
{
    const Result<bool> result = mClient->hasSubfolders(handle, isRoot);
    return result.success && result.value();
}
