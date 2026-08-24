#pragma once
#include "IMegaClient.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// Thin adapter in front of IMegaClient for the folder-tree side panel: resolves the
// root-vs-handle split (getChildren has no isRoot overload) and applies the
// tree-only rule that files never appear and the panel always lists alphabetically,
// whatever the file list is sorted by.
class FolderTreeService
{
public:
    explicit FolderTreeService(std::shared_ptr<IMegaClient> client);

    void loadSubfolders(std::uint64_t handle,
                        bool isRoot,
                        std::function<void(Result<std::vector<FileEntry>>)> onDone);

    // Synchronous because its one caller is QAbstractItemModel::hasChildren(), which
    // answers the view inline. Collapses the Result to that bool: a handle that no
    // longer resolves simply gets no expand arrow.
    bool hasSubfolders(std::uint64_t handle, bool isRoot) const;

    // Must precede a re-read that is meant to show someone else's changes: the
    // getters above only report what the SDK was last told (IMegaClient.h).
    void syncWithServer(std::function<void(Result<void>)> onDone);

private:
    std::shared_ptr<IMegaClient> mClient;
};
