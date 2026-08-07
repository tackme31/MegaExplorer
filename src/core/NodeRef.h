#pragma once
#include <cstdint>
#include <string>

// The minimum needed to point at a node a copy/move is about to act on. Produced by
// the clipboard and by a Ctrl+drag alike, both from FileListModel::selectedEntries().
//
// name is carried because a copy has to pick one nothing in the destination uses;
// re-resolving every handle at paste time would buy nothing.
//
// Not NodeInfo: that is what a handle resolves to *right now*, hence its inCloud
// flag. This is a snapshot from when the user selected the node, never revalidated.
struct NodeRef
{
    std::string name;
    std::uint64_t handle = 0;
    bool isFolder = false;
};
