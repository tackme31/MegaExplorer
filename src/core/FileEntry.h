#pragma once
#include <cstdint>
#include <string>

struct FileEntry
{
    std::string name;
    std::uint64_t handle = 0;
    std::uint64_t sizeBytes = 0;
    bool isFolder = false;
    std::int64_t modificationTime = 0;
    bool hasThumbnail = false;

    // Value-type equality, field-by-field. Not defaulted via <=> -- this
    // project builds at C++17, not C++20. Added for
    // Phase 6 test assertions (e.g. gmock's EXPECT_CALL(..., someVector)
    // implicitly builds an Eq() matcher over std::vector<FileEntry>, which
    // needs this).
    bool operator==(const FileEntry& other) const
    {
        return name == other.name && handle == other.handle && sizeBytes == other.sizeBytes &&
               isFolder == other.isFolder && modificationTime == other.modificationTime &&
               hasThumbnail == other.hasThumbnail;
    }
};
