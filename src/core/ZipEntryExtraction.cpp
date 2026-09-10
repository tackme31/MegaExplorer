#include "core/ZipEntryExtraction.h"

#include "core/ILocalFileSystem.h"
#include "core/IMegaClient.h"
#include "core/MegaErrorCodes.h"
#include "core/ZipExtract.h"

#include <atomic>
#include <limits>
#include <optional>
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

    // The one member written from outside the SDK thread.
    std::atomic<bool> cancelled{false};

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
            onStreamDone(Result<void>::ok());
            return;
        }
        const std::shared_ptr<State> self = shared_from_this();
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

    bool onChunk(const char* data, std::size_t size)
    {
        if (cancelled || !inflater->feed(data, size))
            return false;
        received += size;
        if (onProgress)
            onProgress(received, entry.compressedSize);
        return true;
    }

    void onStreamDone(Result<void> transfer)
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
                                       std::string destinationPath)
    : mState(std::make_shared<State>())
{
    State& s = *mState;
    s.client = std::move(client);
    s.fileSystem = std::move(fileSystem);
    s.archiveHandle = archiveHandle;
    s.entry = entry;
    s.localHeaderShift = localHeaderShift;
    s.destinationPath = std::move(destinationPath);
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
    mState->cancelled = true;
}
