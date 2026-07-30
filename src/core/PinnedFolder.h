#pragma once
#include <cstdint>
#include <string>

// One quick-access pin. handle is the identity; name is only a cached label
// for display, refreshed from the live node on every login (a MEGA handle is
// stable for the node's whole lifetime, so a rename or a move never
// invalidates a pin -- only deletion does).
//
// No isRoot field, unlike PathSegment/FolderNavigationService::Location: the
// Cloud Drive root is always visible in the tree below, so pinning it would
// be redundant, and FolderPinMenu.qml disables the action for it.
struct PinnedFolder
{
    std::string name;
    std::uint64_t handle = 0;

    // Field-by-field, same rationale as FileEntry's/PathSegment's own
    // operator== (gmock's Eq() matcher over std::vector<PinnedFolder>).
    bool operator==(const PinnedFolder& other) const
    {
        return name == other.name && handle == other.handle;
    }
};
