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
};
