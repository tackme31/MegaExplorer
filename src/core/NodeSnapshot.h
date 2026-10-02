#pragma once
#include <cstdint>
#include <string>
#include <vector>

// One node with everything a plugin's items.* reads return
// (docs/investigations/STUDY_PLUGIN_V1_DESIGN.md §6-2). Handles are the SDK's
// real ones throughout -- the Cloud Drive root included, never the 0 sentinel.
struct NodeSnapshot
{
    std::uint64_t handle = 0;
    std::string name;
    bool isFolder = false;
    // 0 for a folder.
    std::uint64_t sizeBytes = 0;
    std::int64_t modificationTime = 0;
    bool hasParent = false;
    std::uint64_t parentHandle = 0;
    // Root-relative, "/" for the root itself; folders have no trailing slash.
    std::string path;
    bool isFavourite = false;
    std::string description;
    std::vector<std::string> tags;

    bool operator==(const NodeSnapshot& other) const
    {
        return handle == other.handle && name == other.name && isFolder == other.isFolder &&
               sizeBytes == other.sizeBytes && modificationTime == other.modificationTime &&
               hasParent == other.hasParent && parentHandle == other.parentHandle &&
               path == other.path && isFavourite == other.isFavourite &&
               description == other.description && tags == other.tags;
    }
};

// What a whole-subtree listing keeps per node: building a full NodeSnapshot costs a
// path walk to the root each.
struct DescendantNode
{
    std::uint64_t handle = 0;
    bool isFolder = false;
};
