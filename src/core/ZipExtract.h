#pragma once
#include "core/ZipListing.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

// Pulling one entry's data out of a zip (docs/investigations/STUDY_ARCHIVE_EXTRACTION.md
// sections 3 and 5). zlib enters MegaExplorerCore through this file only, so that
// ZipListing stays dependency-free.

enum class ZipEntrySupport
{
    Supported,
    Directory,
    Encrypted,
    UnsupportedMethod, // anything but stored (0) and deflate (8)
};

ZipEntrySupport zipEntrySupport(const ZipEntry& entry);

constexpr std::size_t kZipLocalHeaderFixedSize = 30;

// localHeader is the archive's bytes from localHeaderOffset on, at least
// kZipLocalHeaderFixedSize of them. Returns the file offset of the entry's data.
// The local extra field's length need not match the central directory's, which is
// why this has to read the local header rather than compute from ZipEntry.
std::optional<std::uint64_t> zipEntryDataOffset(const std::vector<char>& localHeader,
                                                std::uint64_t localHeaderOffset);

enum class ZipExtractStatus
{
    Pending,
    Ok,
    Unsupported,
    Truncated, // finish() came before compressedSize bytes were fed
    CorruptData,
    SizeMismatch, // the data disagrees with the sizes in the central directory
    CrcMismatch,
    SinkFailed,
};

// Decompresses one entry from its compressed bytes, fed in archive order and split
// anywhere, handing the output to sink as it is produced.
class ZipEntryInflater
{
public:
    // Returning false stops the extraction with SinkFailed.
    using Sink = std::function<bool(const char* data, std::size_t size)>;

    ZipEntryInflater(const ZipEntry& entry, Sink sink);
    ~ZipEntryInflater();
    ZipEntryInflater(const ZipEntryInflater&) = delete;
    ZipEntryInflater& operator=(const ZipEntryInflater&) = delete;

    // False once the extraction has failed; status() says why.
    bool feed(const char* data, std::size_t size);
    // Call after the last byte: checks the sizes and the CRC-32.
    ZipExtractStatus finish();
    ZipExtractStatus status() const;

private:
    struct State;
    std::unique_ptr<State> mState;
};
