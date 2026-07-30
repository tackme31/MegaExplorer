#pragma once
#include <cstdint>
#include <string>

// A single node's current identity, as resolved from its handle alone --
// what IMegaClient::getNodeInfo returns.
//
// inCloud distinguishes a live Cloud Drive node from one sitting in the
// Rubbish bin or the Vault. A MEGA "delete" is really a move to the Rubbish
// bin, so the handle stays resolvable afterwards; without this flag a caller
// holding only a handle (Phase 11's pinned folders) cannot tell the two
// apart. IMegaClient::getPath can't be used for this -- it normalizes
// whatever root the parent walk ends at into the Cloud Drive sentinel.
struct NodeInfo
{
    std::string name;
    std::uint64_t handle = 0;
    bool isFolder = false;
    bool inCloud = false;

    // Field-by-field, not <=>-defaulted -- same rationale as FileEntry's and
    // PathSegment's own operator== (gmock builds an Eq() matcher over this,
    // and CMAKE_CXX_STANDARD isn't pinned to C++20+).
    bool operator==(const NodeInfo& other) const
    {
        return name == other.name && handle == other.handle && isFolder == other.isFolder &&
               inCloud == other.inCloud;
    }
};
