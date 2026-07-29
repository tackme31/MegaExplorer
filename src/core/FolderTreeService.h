#pragma once
#include "IMegaClient.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// Thin adapter in front of IMegaClient for the folder-tree side panel
// (FolderTreeModel, src/qml): resolves the isRoot/getRootChildren vs.
// getChildren split IMegaClient exposes (unlike getPath/search, getChildren
// has no isRoot overload) and applies the tree-only domain rule -- files
// never appear in the side panel, and the panel always lists alphabetically
// regardless of whatever sort order the file-list view is currently using.
// Qt-free (MegaExplorerCore) like the other *Service classes, so it's
// testable against MockMegaClient without a GUI.
class FolderTreeService
{
public:
    explicit FolderTreeService(std::shared_ptr<IMegaClient> client);

    // Fetches handle's (or the root's, when isRoot) children and filters the
    // result down to folders only, always sorted by name ascending -- the
    // tree's own domain rule, independent of the file-list view's sort
    // order. handle is ignored when isRoot is true, same sentinel convention
    // as IMegaClient::getPath/search.
    void loadSubfolders(std::uint64_t handle,
                        bool isRoot,
                        std::function<void(Result<std::vector<FileEntry>>)> onDone);

private:
    std::shared_ptr<IMegaClient> mClient;
};
