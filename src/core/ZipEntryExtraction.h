#pragma once
#include "core/Result.h"
#include "core/ZipListing.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class IMegaClient;
class ILocalFileSystem;

// App-defined error code (MegaErrorCodes.h): the archive's own bytes are at fault --
// an entry that cannot be extracted, a damaged local header, corrupt data, or a size
// or CRC mismatch.
constexpr int kArchiveEntryInvalid = 4;

// Writes one entry of a zip on MEGA to a local file, transferring only that entry's
// compressed bytes (docs/investigations/STUDY_ARCHIVE_EXTRACTION.md section 4.2).
// A file appears at destinationPath only once every byte arrived and the CRC-32
// matched; a failure, a cancel or a mismatch leaves nothing under that name.
//
// Inflating and writing happen inside the SDK's data callback, on its thread, and so
// do onProgress and onDone.
class ZipEntryExtraction
{
public:
    using Progress = std::function<void(std::uint64_t received, std::uint64_t total)>;

    // localHeaderShift is the archive's ZipDirectoryLocation::localHeaderShift.
    ZipEntryExtraction(std::shared_ptr<IMegaClient> client,
                       std::shared_ptr<ILocalFileSystem> fileSystem,
                       std::uint64_t archiveHandle,
                       const ZipEntry& entry,
                       std::uint64_t localHeaderShift,
                       std::string destinationPath);
    // Cancels, so an extraction its owner has dropped does not run on to the end.
    ~ZipEntryExtraction();
    ZipEntryExtraction(const ZipEntryExtraction&) = delete;
    ZipEntryExtraction& operator=(const ZipEntryExtraction&) = delete;

    // Call once. Progress counts compressed bytes; onDone fires exactly once.
    void start(Progress onProgress, std::function<void(Result<void>)> onDone);
    // Any thread. Takes hold at the next chunk, and onDone then fails with kEIncomplete.
    void cancel();

private:
    struct State;
    std::shared_ptr<State> mState;
};
