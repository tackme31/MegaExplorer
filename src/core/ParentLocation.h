#pragma once
#include <cstdint>

// The folder a node sits in, in the (handle, isRoot) form every move is addressed
// by. The Rubbish bin's top is (0, true) like the Cloud Drive root, so
// isRubbishBin tells the two apart: a move to (0, true) lands in the Cloud Drive.
struct ParentLocation
{
    std::uint64_t handle = 0;
    bool isRoot = false;
    bool isRubbishBin = false;
};
