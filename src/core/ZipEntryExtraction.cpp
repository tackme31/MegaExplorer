#include "core/ZipEntryExtraction.h"

#include "core/ILocalFileSystem.h"
#include "core/IMegaClient.h"
#include "core/MegaErrorCodes.h"
#include "core/ZipExtract.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace
{

Result<void> invalid(std::string why)
{
    return Result<void>::fail(std::move(why), kArchiveEntryInvalid);
}

Result<void> cancelledResult()
{
    return Result<void>::fail("Extraction cancelled", MegaErrorCode::kEIncomplete);
}

Result<void> writeFailed()
{
    return Result<void>::fail("Could not write the destination file", MegaErrorCode::kEFailed);
}

std::optional<Result<void>> refusalFor(ZipEntrySupport support)
{
    switch (support)
    {
        case ZipEntrySupport::Supported:
            return std::nullopt;
        case ZipEntrySupport::Directory:
            return invalid("The entry is a folder");
        case ZipEntrySupport::Encrypted:
            return invalid("The entry is encrypted");
        case ZipEntrySupport::UnsupportedMethod:
            return invalid("The entry uses an unsupported compression method");
    }
    return invalid("The entry cannot be extracted");
}

Result<void> resultFor(ZipExtractStatus status)
{
    switch (status)
    {
        case ZipExtractStatus::Ok:
            return Result<void>::ok();
        case ZipExtractStatus::SinkFailed:
            return writeFailed();
        case ZipExtractStatus::Unsupported:
            return invalid("The entry uses an unsupported compression method");
        case ZipExtractStatus::Truncated:
            return invalid("The entry's data ends early");
        case ZipExtractStatus::CorruptData:
            return invalid("The entry's compressed data is corrupt");
        case ZipExtractStatus::SizeMismatch:
            return invalid("The entry's size disagrees with the archive's directory");
        case ZipExtractStatus::CrcMismatch:
            return invalid("The entry failed its CRC-32 check");
        case ZipExtractStatus::Pending:
            break;
    }
    return Result<void>::fail("Extraction ended unfinished", MegaErrorCode::kEInternal);
}

} // namespace

struct ZipEntryExtraction::State : std::enable_shared_from_this<State>
{
    std::shared_ptr<IMegaClient> client;
    std::shared_ptr<ILocalFileSystem> fileSystem;
    std::uint64_t archiveHandle = 0;
    ZipEntry entry;
    std::uint64_t localHeaderShift = 0;
    std::string destinationPath;
    std::size_t bufferLimit = kZipExtractionBufferLimit;

    std::atomic<bool> cancelled{false};

    // Guarded by queueMutex. The members after these are the worker's alone once it
    // has started.
    std::mutex queueMutex;
    std::condition_variable queueChanged;
    std::deque<std::vector<char>> pieces;
    std::size_t queuedBytes = 0;
    bool workerStopped = false;
    std::optional<Result<void>> transferResult;

    Progress onProgress;
    std::function<void(Result<void>)> onDone;
    std::unique_ptr<ILocalFileWriter> writer;
    std::unique_ptr<ZipEntryInflater> inflater;
    std::uint64_t received = 0;

    void finish(Result<void> result)
    {
        inflater.reset();
        // Before onDone, so the caller never sees an uncommitted temporary file.
        writer.reset();
        onProgress = nullptr;
        std::function<void(Result<void>)> done = std::move(onDone);
        onDone = nullptr;
        if (done)
            done(std::move(result));
    }

    void requestCancel()
    {
        cancelled = true;
        // Taking the lock orders the store before any waiter's next look at the flag.
        {
            std::lock_guard<std::mutex> lock(queueMutex);
        }
        queueChanged.notify_all();
    }

    void onLocalHeader(std::uint64_t localHeaderOffset, Result<std::vector<char>> header)
    {
        if (cancelled)
        {
            finish(cancelledResult());
            return;
        }
        if (!header.success)
        {
            // kEArgs is the range starting past the end of the node: damage, not I/O.
            finish(header.errorCode == MegaErrorCode::kEArgs
                       ? invalid("The entry's local header lies outside the archive")
                       : Result<void>::fail(header.errorMessage, header.errorCode));
            return;
        }
        const std::optional<std::uint64_t> dataOffset =
            zipEntryDataOffset(header.value(), localHeaderOffset);
        if (!dataOffset)
        {
            finish(invalid("The entry's local header is missing or damaged"));
            return;
        }

        writer = fileSystem->createFile(destinationPath);
        if (!writer)
        {
            finish(Result<void>::fail("Could not create the destination file",
                                      MegaErrorCode::kEFailed));
            return;
        }
        inflater =
            std::make_unique<ZipEntryInflater>(entry, [this](const char* data, std::size_t size) {
                return writer->write(data, size);
            });

        if (entry.compressedSize == 0)
        {
            conclude(Result<void>::ok());
            return;
        }
        const std::shared_ptr<State> self = shared_from_this();
        // Detached: it owns a share of this state and ends once the transfer has, so
        // nothing has to be joined from a thread that might be the SDK's.
        try
        {
            std::thread([self] {
                self->drain();
            }).detach();
        }
        catch (const std::system_error&)
        {
            finish(Result<void>::fail("Could not start the extraction", MegaErrorCode::kEFailed));
            return;
        }
        client->readFileRangeStreamed(
            archiveHandle,
            *dataOffset,
            entry.compressedSize,
            [self](const char* data, std::size_t size) {
                return self->onChunk(data, size);
            },
            [self](Result<void> transfer) {
                self->onStreamDone(std::move(transfer));
            });
    }

    // SDK thread. The copy is unavoidable: the SDK reuses its buffer once this returns.
    bool onChunk(const char* data, std::size_t size)
    {
        if (cancelled)
            return false;
        std::vector<char> piece(data, data + size);
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueChanged.wait(lock, [&] {
                return cancelled || workerStopped || queuedBytes == 0 ||
                       queuedBytes + size <= bufferLimit;
            });
            if (cancelled || workerStopped)
                return false;
            pieces.push_back(std::move(piece));
            queuedBytes += size;
        }
        queueChanged.notify_all();
        return true;
    }

    // SDK thread.
    void onStreamDone(Result<void> transfer)
    {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            transferResult = std::move(transfer);
        }
        queueChanged.notify_all();
    }

    // Worker thread. The verdict waits for the transfer's end even after a failure here,
    // so that onDone still means nothing more is moving.
    void drain()
    {
        for (;;)
        {
            std::vector<char> piece;
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                queueChanged.wait(lock, [&] {
                    return cancelled || !pieces.empty() || transferResult.has_value();
                });
                if (cancelled || pieces.empty())
                    break;
                piece = std::move(pieces.front());
                pieces.pop_front();
                queuedBytes -= piece.size();
            }
            queueChanged.notify_all();

            if (!inflater->feed(piece.data(), piece.size()))
                break;
            received += piece.size();
            if (onProgress)
                onProgress(received, entry.compressedSize);
        }

        Result<void> transfer = Result<void>::ok();
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            workerStopped = true;
            pieces.clear();
            queuedBytes = 0;
            queueChanged.notify_all();
            queueChanged.wait(lock, [&] {
                return transferResult.has_value();
            });
            transfer = std::move(*transferResult);
        }
        conclude(std::move(transfer));
    }

    void conclude(Result<void> transfer)
    {
        // Ahead of the transfer's own result: refusing a chunk fails the transfer too,
        // and its message would hide why.
        if (cancelled)
        {
            finish(cancelledResult());
            return;
        }
        if (inflater->status() != ZipExtractStatus::Pending)
        {
            finish(resultFor(inflater->status()));
            return;
        }
        if (!transfer.success)
        {
            finish(transfer.errorCode == MegaErrorCode::kEArgs
                       ? invalid("The entry's data lies outside the archive")
                       : std::move(transfer));
            return;
        }
        const ZipExtractStatus status = inflater->finish();
        if (status != ZipExtractStatus::Ok)
            finish(resultFor(status));
        else if (!writer->commit())
            finish(writeFailed());
        else
            finish(Result<void>::ok());
    }
};

ZipEntryExtraction::ZipEntryExtraction(std::shared_ptr<IMegaClient> client,
                                       std::shared_ptr<ILocalFileSystem> fileSystem,
                                       std::uint64_t archiveHandle,
                                       const ZipEntry& entry,
                                       std::uint64_t localHeaderShift,
                                       std::string destinationPath,
                                       std::size_t bufferLimit)
    : mState(std::make_shared<State>())
{
    State& s = *mState;
    s.client = std::move(client);
    s.fileSystem = std::move(fileSystem);
    s.archiveHandle = archiveHandle;
    s.entry = entry;
    s.localHeaderShift = localHeaderShift;
    s.destinationPath = std::move(destinationPath);
    s.bufferLimit = bufferLimit;
}

ZipEntryExtraction::~ZipEntryExtraction()
{
    cancel();
}

void ZipEntryExtraction::start(Progress onProgress, std::function<void(Result<void>)> onDone)
{
    State& s = *mState;
    s.onProgress = std::move(onProgress);
    s.onDone = std::move(onDone);

    if (std::optional<Result<void>> refusal = refusalFor(zipEntrySupport(s.entry)))
    {
        s.finish(std::move(*refusal));
        return;
    }
    if (s.entry.localHeaderOffset >
        (std::numeric_limits<std::uint64_t>::max)() - s.localHeaderShift)
    {
        s.finish(invalid("The entry's local header lies outside the archive"));
        return;
    }

    const std::uint64_t localHeaderOffset = s.entry.localHeaderOffset + s.localHeaderShift;
    const std::shared_ptr<State> self = mState;
    s.client->readFileRange(s.archiveHandle,
                            localHeaderOffset,
                            kZipLocalHeaderFixedSize,
                            [self, localHeaderOffset](Result<std::vector<char>> header) {
                                self->onLocalHeader(localHeaderOffset, std::move(header));
                            });
}

void ZipEntryExtraction::cancel()
{
    mState->requestCancel();
}

namespace
{

// Unique per extraction, so two entries sharing a leaf name never share a staging file.
std::string stagingPathFor(const std::string& destinationPath)
{
    static std::atomic<std::uint64_t> counter{0};
    const auto stamp = static_cast<unsigned long long>(
        std::chrono::system_clock::now().time_since_epoch().count());
    return destinationPath + "." + std::to_string(stamp) + "-" + std::to_string(++counter) +
           ".extracting";
}

} // namespace

ZipEntryDownloadRunner::ZipEntryDownloadRunner(std::shared_ptr<IMegaClient> client,
                                               std::shared_ptr<ILocalFileSystem> fileSystem,
                                               std::uint64_t archiveHandle,
                                               const ZipEntry& entry,
                                               std::uint64_t localHeaderShift,
                                               std::string destinationPath)
    : mFileSystem(fileSystem), mDestinationPath(std::move(destinationPath)),
      mStagingPath(stagingPathFor(mDestinationPath)),
      mExtraction(std::make_unique<ZipEntryExtraction>(std::move(client),
                                                       std::move(fileSystem),
                                                       archiveHandle,
                                                       entry,
                                                       localHeaderShift,
                                                       mStagingPath))
{}

void ZipEntryDownloadRunner::start(std::function<void(std::uint64_t, std::uint64_t)> onProgress,
                                   std::function<void(Result<DownloadOutcome>)> onDone)
{
    // Copies rather than this: DownloadService may drop the last reference to the runner
    // inside onDone, and a runner holding itself would leak when a transfer never ends.
    mExtraction->start(std::move(onProgress),
                       [fileSystem = mFileSystem,
                        staging = mStagingPath,
                        destination = mDestinationPath,
                        onDone = std::move(onDone)](Result<void> extracted) {
                           if (!extracted.success)
                           {
                               onDone(Result<DownloadOutcome>::fail(extracted.errorMessage,
                                                                    extracted.errorCode));
                               return;
                           }
                           std::optional<std::string> saved =
                               fileSystem->moveToFreeName(staging, destination);
                           if (!saved)
                           {
                               onDone(Result<DownloadOutcome>::fail(
                                   "Could not name the destination file", MegaErrorCode::kEFailed));
                               return;
                           }
                           onDone(Result<DownloadOutcome>::ok(DownloadOutcome{std::move(*saved)}));
                       });
}

void ZipEntryDownloadRunner::cancel()
{
    mExtraction->cancel();
}
