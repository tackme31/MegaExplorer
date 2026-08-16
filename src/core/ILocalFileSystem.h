#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct LocalEntry
{
    std::string path; // absolute, native separators (the SDK's LocalPath splits on '\')
    std::string name; // leaf
    bool isDirectory = false;
    std::uint64_t sizeBytes = 0; // 0 for a directory
};

// Local-side counterpart of IMegaClient: the only way src/core reads the local
// filesystem. One level at a time on purpose -- the upload scan descends only
// into branches that collide, so a recursive listing would cost far more than
// the answer needs (SPEC_NAME_CONFLICT_UPLOAD.md 5-1).
//
// MegaExplorerCore links no Qt, hence std::string rather than QString.
class ILocalFileSystem
{
public:
    virtual ~ILocalFileSystem() = default;

    // Nullopt when the path does not exist. Hidden files are ordinary entries:
    // the SDK's recursive upload sends them, so a scan that skipped them would
    // under-count the collisions the user is asked about.
    virtual std::optional<LocalEntry> entryFor(const std::string& path) const = 0;

    // Direct children only, in no particular order. Empty for a file, an
    // unreadable directory, or a missing path.
    virtual std::vector<LocalEntry> listDirectory(const std::string& path) const = 0;
};
