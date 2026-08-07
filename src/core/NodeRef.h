#pragma once
#include <cstdint>
#include <string>

// The minimum needed to point at a node a copy/move is about to act on.
// Produced by the clipboard and by a Ctrl+drag alike -- both start from
// FileListModel::selectedEntries()' {handle, name, isFolder} maps, and the
// drag never touches the clipboard.
//
// name is carried because a copy has to pick one nothing in the destination
// is using (FileOperationService::uniqueCopyName); re-resolving every handle
// at paste time would buy nothing.
//
// Not NodeInfo: that is what IMegaClient::getNodeInfo resolves a handle to
// right now, hence its inCloud flag. This is a snapshot taken when the user
// selected the node, and nothing here is resolved or revalidated.
struct NodeRef
{
    std::string name;
    std::uint64_t handle = 0;
    bool isFolder = false;
};
