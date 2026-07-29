#pragma once
#include <cstdint>
#include <string>

// One breadcrumb segment. isRoot follows FolderNavigationService::Location's
// root-sentinel convention (handle is meaningless when isRoot is true).
struct PathSegment
{
    std::string name;
    std::uint64_t handle = 0;
    bool isRoot = false;

    // Value-type equality, field-by-field -- same rationale as FileEntry's
    // own operator== (gmock's Eq() matcher over std::vector<PathSegment>,
    // and FolderNavigationController's own change-detection before it
    // re-emits breadcrumbChanged).
    bool operator==(const PathSegment& other) const
    {
        return name == other.name && handle == other.handle && isRoot == other.isRoot;
    }
};
