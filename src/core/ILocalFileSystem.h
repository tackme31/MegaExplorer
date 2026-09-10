#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
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

// A file that appears at its path only once commit() succeeds: until then the bytes
// go to a temporary name beside it, and dropping the writer removes them.
class ILocalFileWriter
{
public:
    virtual ~ILocalFileWriter() = default;
    virtual bool write(const char* data, std::size_t size) = 0;
    // Replaces a file already at the path; picking a free name is the caller's job.
    virtual bool commit() = 0;
};

// Local-side counterpart of IMegaClient: the only way src/core touches the local
// filesystem. One level at a time on purpose -- the upload scan descends only
// into branches that collide, so a recursive listing would cost far more than
// the answer needs (SPEC_NAME_CONFLICT_UPLOAD.md 5-1).
//
// MegaExplorerCore links no Qt, hence std::string rather than QString.
class ILocalFileSystem
{
public:
    virtual ~ILocalFileSystem() = default;

    // Nullptr when the file cannot be started (a missing directory, no permission).
    // One thread at a time may use the writer, not necessarily the creating one.
    virtual std::unique_ptr<ILocalFileWriter> createFile(const std::string& path) = 0;

    // Nullopt when the path does not exist. Hidden files are ordinary entries:
    // the SDK's recursive upload sends them, so a scan that skipped them would
    // under-count the collisions the user is asked about.
    virtual std::optional<LocalEntry> entryFor(const std::string& path) const = 0;

    // Direct children only, in no particular order. Nullopt when the listing
    // could not be taken at all -- a missing path, a file, or a directory that
    // refused to be read -- which a caller must not confuse with an empty
    // directory: the upload skip plan drops what it cannot see.
    virtual std::optional<std::vector<LocalEntry>> listDirectory(const std::string& path) const = 0;
};
