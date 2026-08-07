#pragma once
#include <cstdint>
#include <string>

// One quick-access pin. handle is the identity; name is a cached display label,
// refreshed on every login -- a handle is stable for the node's whole lifetime, so
// only deletion invalidates a pin, never a rename or a move.
//
// No isRoot field, unlike PathSegment: the root is always visible in the tree, so
// pinning it would be redundant and QML disables the action for it.
struct PinnedFolder
{
    std::string name;
    std::uint64_t handle = 0;

    // Field-by-field, as elsewhere.
    bool operator==(const PinnedFolder& other) const
    {
        return name == other.name && handle == other.handle;
    }
};
