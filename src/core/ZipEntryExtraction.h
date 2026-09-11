#pragma once
#include "core/DownloadService.h"
#include "core/Result.h"
#include "core/ZipListing.h"

#include <cstddef>
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
// Inflating and writing run on the extraction's own thread, fed copies of the SDK's
// pieces, so the SDK's one thread stays free for other transfers. onProgress and onDone
// arrive there (a failure before any data, on the caller's or the SDK's thread), and
// onProgress must not wait on the SDK, whose thread may be waiting on it.
//
// The bytes copied but not yet written. Past it the SDK's thread waits for the disk; a
// piece is always taken when nothing is queued, as one can be ~33 MiB (STUDY section 7).
constexpr std::size_t kZipExtractionBufferLimit = 64 * 1024 * 1024;

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
                       std::string destinationPath,
                       std::size_t bufferLimit = kZipExtractionBufferLimit);
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

// Puts one entry's extraction on DownloadService's queue. The entry is written under a
// staging name beside destinationPath and only named once complete: destinationPath if
// it is still free then, otherwise "stem (N).ext" -- the same end-of-transfer choice
// the SDK makes for a plain download, so neither can replace the other's file.
class ZipEntryDownloadRunner final : public DownloadRunner
{
public:
    ZipEntryDownloadRunner(std::shared_ptr<IMegaClient> client,
                           std::shared_ptr<ILocalFileSystem> fileSystem,
                           std::uint64_t archiveHandle,
                           const ZipEntry& entry,
                           std::uint64_t localHeaderShift,
                           std::string destinationPath);

    void start(std::function<void(std::uint64_t, std::uint64_t)> onProgress,
               std::function<void(Result<DownloadOutcome>)> onDone) override;
    void cancel() override;

private:
    std::shared_ptr<ILocalFileSystem> mFileSystem;
    std::string mDestinationPath;
    std::string mStagingPath;
    std::unique_ptr<ZipEntryExtraction> mExtraction;
};
