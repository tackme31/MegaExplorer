#include "core/ZipEntryExtraction.h"

#include "core/ILocalFileSystem.h"
#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <zlib.h>

using ::testing::_;

namespace
{

constexpr std::uint64_t kArchive = 42;
constexpr std::uint64_t kStubSize = 100;
constexpr std::size_t kLocalExtra = 7;
const std::string kDestination = "C:\\out\\sample.txt";

void putU16(std::vector<char>& out, std::uint16_t value)
{
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

void putU32(std::vector<char>& out, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
}

std::string rawDeflate(const std::string& input)
{
    z_stream stream{};
    EXPECT_EQ(
        deflateInit2(&stream, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY),
        Z_OK);
    std::string out(deflateBound(&stream, static_cast<uLong>(input.size())), '\0');
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    EXPECT_EQ(deflate(&stream, Z_FINISH), Z_STREAM_END);
    out.resize(stream.total_out);
    deflateEnd(&stream);
    return out;
}

std::string sampleText()
{
    std::string text;
    for (int i = 0; text.size() < 20000; ++i)
        text += "line " + std::to_string(i * 7919 % 10007) + " of the sample\n";
    return text;
}

// A self-extracting stub, then one local header whose extra field the central
// directory would not know the length of, then the deflated data.
struct Archive
{
    std::string original = sampleText();
    std::vector<char> bytes;
    ZipEntry entry;
    std::uint64_t dataOffset = 0;

    Archive()
    {
        const std::string name = "dir/sample.txt";
        const std::string compressed = rawDeflate(original);
        bytes.assign(kStubSize, 's');
        putU32(bytes, 0x04034b50);
        putU16(bytes, 20); // version needed
        putU16(bytes, 8);  // flags: data descriptor, so the sizes below are zero
        putU16(bytes, 8);  // deflate
        putU32(bytes, 0);  // modified time and date
        putU32(bytes, 0);  // crc-32
        putU32(bytes, 0);  // compressed size
        putU32(bytes, 0);  // uncompressed size
        putU16(bytes, static_cast<std::uint16_t>(name.size()));
        putU16(bytes, static_cast<std::uint16_t>(kLocalExtra));
        bytes.insert(bytes.end(), name.begin(), name.end());
        bytes.insert(bytes.end(), kLocalExtra, 'e');
        dataOffset = bytes.size();
        bytes.insert(bytes.end(), compressed.begin(), compressed.end());

        entry.rawName = name;
        entry.compressionMethod = 8;
        entry.compressedSize = compressed.size();
        entry.uncompressedSize = original.size();
        entry.crc =
            static_cast<std::uint32_t>(crc32(0L,
                                             reinterpret_cast<const Bytef*>(original.data()),
                                             static_cast<uInt>(original.size())));
        entry.localHeaderOffset = 0; // as stored: the stub is what localHeaderShift adds
    }
};

// Opens once; wait() blocks until then.
class Gate
{
public:
    void open()
    {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mOpen = true;
        }
        mChanged.notify_all();
    }
    void wait()
    {
        std::unique_lock<std::mutex> lock(mMutex);
        mChanged.wait(lock, [this] {
            return mOpen;
        });
    }

private:
    std::mutex mMutex;
    std::condition_variable mChanged;
    bool mOpen = false;
};

template<typename Predicate>
bool eventually(Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

class MemoryFileSystem : public ILocalFileSystem
{
public:
    // Written by the extraction's worker; read only after onDone, which orders it.
    std::map<std::string, std::string> committed;
    std::atomic<int> liveWriters{0};
    bool refuseCreate = false;
    bool failWrites = false;

    std::optional<LocalEntry> entryFor(const std::string&) const override
    {
        return std::nullopt;
    }

    std::optional<std::vector<LocalEntry>> listDirectory(const std::string&) const override
    {
        return std::nullopt;
    }

    std::unique_ptr<ILocalFileWriter> createFile(const std::string& path) override
    {
        if (refuseCreate)
            return nullptr;
        return std::make_unique<Writer>(*this, path);
    }

    // Each (from, to) asked for. Like committed, read only after onDone.
    std::vector<std::pair<std::string, std::string>> moves;
    // The name a move lands on; empty means "to" itself was free.
    std::string freeName;

    std::optional<std::string> moveToFreeName(const std::string& from, const std::string& to) override
    {
        moves.emplace_back(from, to);
        const auto staged = committed.find(from);
        if (staged == committed.end())
            return std::nullopt;
        const std::string target = freeName.empty() ? to : freeName;
        std::string bytes = std::move(staged->second);
        committed.erase(staged);
        committed[target] = std::move(bytes);
        return target;
    }

private:
    class Writer : public ILocalFileWriter
    {
    public:
        Writer(MemoryFileSystem& fs, std::string path) : mFs(fs), mPath(std::move(path))
        {
            ++mFs.liveWriters;
        }
        ~Writer() override
        {
            --mFs.liveWriters;
        }
        bool write(const char* data, std::size_t size) override
        {
            if (mFs.failWrites)
                return false;
            mBytes.append(data, size);
            return true;
        }
        bool commit() override
        {
            mFs.committed[mPath] = mBytes;
            return true;
        }

    private:
        MemoryFileSystem& mFs;
        std::string mPath;
        std::string mBytes;
    };
};

struct Fixture
{
    std::shared_ptr<MockMegaClient> client = std::make_shared<MockMegaClient>();
    std::shared_ptr<MemoryFileSystem> fs = std::make_shared<MemoryFileSystem>();
    Archive archive;
    std::size_t chunk = 97;
    std::size_t bufferLimit = kZipExtractionBufferLimit;
    std::atomic<int> chunksServed{0};
    std::atomic<int> chunksAccepted{0};
    std::optional<Result<void>> failAfterFirstChunk;
    std::function<void()> afterAcceptedChunk;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> streamed;
    // Serving from a thread of its own, as the SDK does, lets a test hold the worker
    // while the "SDK" is waiting on it.
    bool serveOnOwnThread = false;
    std::thread server;

    ~Fixture()
    {
        if (server.joinable())
            server.join();
        // The detached worker lets go of the state just after onDone; without this the
        // mock could be destroyed on it while the next test runs.
        EXPECT_TRUE(eventually([this] {
            return client.use_count() == 1 && fs.use_count() == 1;
        }));
    }

    std::uint64_t totalChunks() const
    {
        return (archive.entry.compressedSize + chunk - 1) / chunk;
    }

    void streamPieces(std::uint64_t offset,
                      std::uint64_t length,
                      const std::function<bool(const char*, std::size_t)>& onChunk,
                      const std::function<void(Result<void>)>& onDone)
    {
        length = std::min<std::uint64_t>(length, archive.bytes.size() - offset);
        const char* data = archive.bytes.data() + offset;
        for (std::uint64_t at = 0; at < length; at += chunk)
        {
            ++chunksServed;
            const auto size = static_cast<std::size_t>(std::min<std::uint64_t>(chunk, length - at));
            if (!onChunk(data + at, size))
            {
                onDone(Result<void>::fail("aborted", MegaErrorCode::kEIncomplete));
                return;
            }
            ++chunksAccepted;
            if (afterAcceptedChunk)
                afterAcceptedChunk();
            if (failAfterFirstChunk)
            {
                onDone(*failAfterFirstChunk);
                return;
            }
        }
        onDone(Result<void>::ok());
    }

    // Mimics the SDK: pieces in order, and a refused piece fails the transfer.
    void serve()
    {
        ON_CALL(*client, readFileRange(kArchive, _, _, _))
            .WillByDefault([this](std::uint64_t,
                                  std::uint64_t offset,
                                  std::uint64_t length,
                                  std::function<void(Result<std::vector<char>>)> onDone) {
                const std::vector<char>& file = archive.bytes;
                const auto from = static_cast<std::ptrdiff_t>(offset);
                const auto count = static_cast<std::ptrdiff_t>(
                    std::min<std::uint64_t>(length, file.size() - offset));
                onDone(Result<std::vector<char>>::ok(
                    std::vector<char>(file.begin() + from, file.begin() + from + count)));
            });
        ON_CALL(*client, readFileRangeStreamed(kArchive, _, _, _, _))
            .WillByDefault([this](std::uint64_t,
                                  std::uint64_t offset,
                                  std::uint64_t length,
                                  std::function<bool(const char*, std::size_t)> onChunk,
                                  std::function<void(Result<void>)> onDone) {
                streamed.emplace_back(offset, length);
                if (!serveOnOwnThread)
                {
                    streamPieces(offset, length, onChunk, onDone);
                    return;
                }
                server = std::thread([this, offset, length, onChunk, onDone] {
                    streamPieces(offset, length, onChunk, onDone);
                });
            });
    }

    std::unique_ptr<ZipEntryExtraction> make()
    {
        return std::make_unique<ZipEntryExtraction>(
            client, fs, kArchive, archive.entry, kStubSize, kDestination, bufferLimit);
    }
};

// onDone arrives on the extraction's worker, so the test waits for it.
class Outcome
{
public:
    void set(Result<void> result)
    {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mResult = std::move(result);
        }
        mReady.notify_all();
    }
    bool arrived()
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mResult.has_value();
    }
    Result<void> wait()
    {
        std::unique_lock<std::mutex> lock(mMutex);
        const bool finished = mReady.wait_for(lock, std::chrono::seconds(10), [this] {
            return mResult.has_value();
        });
        EXPECT_TRUE(finished);
        return mResult.value_or(Result<void>::fail("never finished", 0));
    }

private:
    std::mutex mMutex;
    std::condition_variable mReady;
    std::optional<Result<void>> mResult;
};

std::shared_ptr<Outcome> begin(ZipEntryExtraction& extraction,
                               const ZipEntryExtraction::Progress& progress = {})
{
    auto outcome = std::make_shared<Outcome>();
    extraction.start(progress, [outcome](Result<void> r) {
        outcome->set(std::move(r));
    });
    return outcome;
}

Result<void> run(ZipEntryExtraction& extraction, const ZipEntryExtraction::Progress& progress = {})
{
    return begin(extraction, progress)->wait();
}

} // namespace

TEST(ZipEntryExtractionTest, WritesTheEntryFromItsOwnBytesOnly)
{
    // Arrange
    Fixture f;
    f.serve();
    EXPECT_CALL(*f.client, readFileRange(kArchive, kStubSize, 30, _));
    EXPECT_CALL(*f.client, readFileRangeStreamed(kArchive, _, _, _, _));
    std::uint64_t lastReceived = 0;
    const auto extraction = f.make();

    // Act
    const Result<void> result = run(*extraction, [&](std::uint64_t received, std::uint64_t total) {
        EXPECT_GT(received, lastReceived);
        EXPECT_EQ(total, f.archive.entry.compressedSize);
        lastReceived = received;
    });

    // Assert
    ASSERT_TRUE(result.success) << result.errorMessage;
    EXPECT_EQ(f.fs->committed[kDestination], f.archive.original);
    ASSERT_EQ(f.streamed.size(), 1u);
    EXPECT_EQ(f.streamed[0].first, f.archive.dataOffset);
    EXPECT_EQ(f.streamed[0].second, f.archive.entry.compressedSize);
    EXPECT_EQ(lastReceived, f.archive.entry.compressedSize);
    EXPECT_EQ(f.fs->liveWriters.load(), 0);
}

TEST(ZipEntryExtractionTest, LeavesNothingWhenTheCrcDoesNotMatch)
{
    Fixture f;
    f.archive.entry.crc ^= 1;
    f.serve();
    const auto extraction = f.make();

    const Result<void> result = run(*extraction);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, kArchiveEntryInvalid);
    EXPECT_TRUE(f.fs->committed.empty());
    EXPECT_EQ(f.fs->liveWriters.load(), 0);
}

TEST(ZipEntryExtractionTest, CancelStopsTheTransferAtTheNextChunk)
{
    Fixture f;
    std::unique_ptr<ZipEntryExtraction> extraction;
    f.afterAcceptedChunk = [&] {
        extraction->cancel();
    };
    f.serve();
    extraction = f.make();

    const Result<void> result = run(*extraction);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, MegaErrorCode::kEIncomplete);
    EXPECT_EQ(f.chunksServed.load(), 2); // the one taken before the cancel, and the one refused
    EXPECT_TRUE(f.fs->committed.empty());
    EXPECT_EQ(f.fs->liveWriters.load(), 0);
}

TEST(ZipEntryExtractionTest, CancelFromTheWorkerSideEndsIncompleteAndLeavesNothing)
{
    Fixture f;
    f.serve();
    const auto extraction = f.make();

    const Result<void> result = run(*extraction, [&](std::uint64_t, std::uint64_t) {
        extraction->cancel();
    });

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, MegaErrorCode::kEIncomplete);
    EXPECT_TRUE(f.fs->committed.empty());
    EXPECT_EQ(f.fs->liveWriters.load(), 0);
}

TEST(ZipEntryExtractionTest, TheTransferDoesNotWaitForTheWriting)
{
    // Arrange: the worker stops at its first piece until the gate opens.
    Fixture f;
    f.serve();
    Gate gate;
    const auto extraction = f.make();

    // Act
    const std::shared_ptr<Outcome> outcome =
        begin(*extraction, [&gate](std::uint64_t, std::uint64_t) {
            gate.wait();
        });

    // Assert: every piece was taken and the transfer ended with the worker still held.
    EXPECT_EQ(static_cast<std::uint64_t>(f.chunksAccepted), f.totalChunks());
    EXPECT_FALSE(outcome->arrived());
    gate.open();
    const Result<void> result = outcome->wait();
    ASSERT_TRUE(result.success) << result.errorMessage;
    EXPECT_EQ(f.fs->committed[kDestination], f.archive.original);
    EXPECT_EQ(f.fs->liveWriters.load(), 0);
}

TEST(ZipEntryExtractionTest, TheTransferWaitsOnceTheBufferIsFull)
{
    // Arrange: room for two pieces of 97 bytes but not three.
    Fixture f;
    f.bufferLimit = 200;
    f.serveOnOwnThread = true;
    f.serve();
    Gate gate;
    const auto extraction = f.make();

    // Act
    const std::shared_ptr<Outcome> outcome =
        begin(*extraction, [&gate](std::uint64_t, std::uint64_t) {
            gate.wait();
        });

    // Assert: the worker holds the first piece, two more wait in the buffer, and the
    // fourth is held back until the worker moves.
    EXPECT_TRUE(eventually([&] {
        return f.chunksServed == 4;
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    EXPECT_EQ(f.chunksAccepted.load(), 3);
    EXPECT_FALSE(outcome->arrived());
    gate.open();
    const Result<void> result = outcome->wait();
    ASSERT_TRUE(result.success) << result.errorMessage;
    EXPECT_EQ(static_cast<std::uint64_t>(f.chunksAccepted), f.totalChunks());
    EXPECT_EQ(f.fs->committed[kDestination], f.archive.original);
}

TEST(ZipEntryExtractionTest, APieceLargerThanTheBufferStillGoesThrough)
{
    Fixture f;
    f.bufferLimit = 10;
    f.serve();
    const auto extraction = f.make();

    const Result<void> result = run(*extraction);

    ASSERT_TRUE(result.success) << result.errorMessage;
    EXPECT_EQ(f.fs->committed[kDestination], f.archive.original);
}

TEST(ZipEntryExtractionTest, PassesATransferFailureThroughAndLeavesNothing)
{
    Fixture f;
    f.failAfterFirstChunk = Result<void>::fail("connection reset", MegaErrorCode::kEAgain);
    f.serve();
    const auto extraction = f.make();

    const Result<void> result = run(*extraction);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, MegaErrorCode::kEAgain);
    EXPECT_TRUE(f.fs->committed.empty());
    EXPECT_EQ(f.fs->liveWriters.load(), 0);
}

TEST(ZipEntryExtractionTest, RefusesAnEncryptedEntryWithoutTransferring)
{
    Fixture f;
    f.archive.entry.encrypted = true;
    f.serve();
    EXPECT_CALL(*f.client, readFileRange(_, _, _, _)).Times(0);
    const auto extraction = f.make();

    const Result<void> result = run(*extraction);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, kArchiveEntryInvalid);
}

TEST(ZipEntryExtractionTest, RejectsADamagedLocalHeaderBeforeTheDataTransfer)
{
    Fixture f;
    f.archive.bytes[kStubSize] = 'X';
    f.serve();
    EXPECT_CALL(*f.client, readFileRangeStreamed(_, _, _, _, _)).Times(0);
    const auto extraction = f.make();

    const Result<void> result = run(*extraction);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, kArchiveEntryInvalid);
}

TEST(ZipEntryExtractionTest, DoesNotTransferWhenTheFileCannotBeCreated)
{
    Fixture f;
    f.fs->refuseCreate = true;
    f.serve();
    EXPECT_CALL(*f.client, readFileRangeStreamed(_, _, _, _, _)).Times(0);
    const auto extraction = f.make();

    const Result<void> result = run(*extraction);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, MegaErrorCode::kEFailed);
}

TEST(ZipEntryExtractionTest, WritesAnEmptyEntryWithoutADataTransfer)
{
    Fixture f;
    f.archive.entry.compressionMethod = 0;
    f.archive.entry.compressedSize = 0;
    f.archive.entry.uncompressedSize = 0;
    f.archive.entry.crc = 0;
    f.serve();
    EXPECT_CALL(*f.client, readFileRangeStreamed(_, _, _, _, _)).Times(0);
    const auto extraction = f.make();

    const Result<void> result = run(*extraction);

    ASSERT_TRUE(result.success) << result.errorMessage;
    ASSERT_EQ(f.fs->committed.count(kDestination), 1u);
    EXPECT_TRUE(f.fs->committed[kDestination].empty());
}

TEST(ZipEntryExtractionTest, AnArchiveCutShortOfTheEntryLeavesNothing)
{
    Fixture f;
    f.archive.bytes.resize(static_cast<std::size_t>(f.archive.dataOffset) +
                           static_cast<std::size_t>(f.archive.entry.compressedSize / 2));
    f.serve();
    const auto extraction = f.make();

    const Result<void> result = run(*extraction);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, kArchiveEntryInvalid);
    EXPECT_TRUE(f.fs->committed.empty());
    EXPECT_EQ(f.fs->liveWriters.load(), 0);
}

TEST(ZipEntryExtractionTest, AWriteFailureStopsTheTransfer)
{
    // A buffer of one piece keeps the "SDK" at most two pieces ahead of the worker.
    Fixture f;
    f.fs->failWrites = true;
    f.bufferLimit = 1;
    f.serve();
    const auto extraction = f.make();

    const Result<void> result = run(*extraction);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, MegaErrorCode::kEFailed);
    EXPECT_LE(f.chunksServed.load(), 3);
    EXPECT_TRUE(f.fs->committed.empty());
    EXPECT_EQ(f.fs->liveWriters.load(), 0);
}

namespace
{

// A runner's onDone, like the extraction's, arrives on the worker.
class RunnerOutcome
{
public:
    void set(Result<DownloadOutcome> result)
    {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mResult = std::move(result);
        }
        mReady.notify_all();
    }
    Result<DownloadOutcome> wait()
    {
        std::unique_lock<std::mutex> lock(mMutex);
        const bool finished = mReady.wait_for(lock, std::chrono::seconds(10), [this] {
            return mResult.has_value();
        });
        EXPECT_TRUE(finished);
        return mResult.value_or(Result<DownloadOutcome>::fail("never finished", 0));
    }

private:
    std::mutex mMutex;
    std::condition_variable mReady;
    std::optional<Result<DownloadOutcome>> mResult;
};

std::shared_ptr<ZipEntryDownloadRunner> makeRunner(Fixture& f)
{
    return std::make_shared<ZipEntryDownloadRunner>(
        f.client, f.fs, kArchive, f.archive.entry, kStubSize, kDestination);
}

Result<DownloadOutcome> runRunner(DownloadRunner& runner)
{
    auto outcome = std::make_shared<RunnerOutcome>();
    runner.start([](std::uint64_t, std::uint64_t) {},
                 [outcome](Result<DownloadOutcome> result) {
                     outcome->set(std::move(result));
                 });
    return outcome->wait();
}

} // namespace

TEST(ZipEntryDownloadRunnerTest, NamesTheFileOnlyOnceTheEntryIsComplete)
{
    Fixture f;
    f.serve();
    const auto runner = makeRunner(f);

    const Result<DownloadOutcome> result = runRunner(*runner);

    ASSERT_TRUE(result.success) << result.errorMessage;
    EXPECT_EQ(result.value().localPath, kDestination);
    ASSERT_EQ(f.fs->committed.size(), 1u);
    EXPECT_EQ(f.fs->committed[kDestination], f.archive.original);
    // Written under a staging name beside the destination, then moved onto it.
    ASSERT_EQ(f.fs->moves.size(), 1u);
    EXPECT_NE(f.fs->moves[0].first, kDestination);
    EXPECT_EQ(f.fs->moves[0].first.rfind(kDestination + ".", 0), 0u);
    EXPECT_EQ(f.fs->moves[0].second, kDestination);
}

TEST(ZipEntryDownloadRunnerTest, ReportsTheNameTheFileWasGivenWhenTheDestinationWasTaken)
{
    Fixture f;
    f.fs->freeName = "C:\\out\\sample (1).txt";
    f.serve();
    const auto runner = makeRunner(f);

    const Result<DownloadOutcome> result = runRunner(*runner);

    ASSERT_TRUE(result.success) << result.errorMessage;
    EXPECT_EQ(result.value().localPath, "C:\\out\\sample (1).txt");
    EXPECT_EQ(f.fs->committed["C:\\out\\sample (1).txt"], f.archive.original);
}

TEST(ZipEntryDownloadRunnerTest, TwoRunnersForOneDestinationNeverShareAStagingFile)
{
    Fixture f;
    f.serve();
    const auto first = makeRunner(f);
    const auto second = makeRunner(f);

    ASSERT_TRUE(runRunner(*first).success);
    ASSERT_TRUE(runRunner(*second).success);

    ASSERT_EQ(f.fs->moves.size(), 2u);
    EXPECT_NE(f.fs->moves[0].first, f.fs->moves[1].first);
}

TEST(ZipEntryDownloadRunnerTest, PassesAFailureThroughWithoutNamingAnything)
{
    Fixture f;
    f.archive.entry.crc ^= 1;
    f.serve();
    const auto runner = makeRunner(f);

    const Result<DownloadOutcome> result = runRunner(*runner);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, kArchiveEntryInvalid);
    EXPECT_TRUE(f.fs->moves.empty());
    EXPECT_TRUE(f.fs->committed.empty());
}

TEST(ZipEntryDownloadRunnerTest, ACancelBeforeStartEndsIncomplete)
{
    Fixture f;
    f.serve();
    EXPECT_CALL(*f.client, readFileRangeStreamed(_, _, _, _, _)).Times(0);
    const auto runner = makeRunner(f);

    runner->cancel();
    const Result<DownloadOutcome> result = runRunner(*runner);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, MegaErrorCode::kEIncomplete);
    EXPECT_TRUE(f.fs->committed.empty());
}

TEST(ZipEntryDownloadRunnerTest, CompletesAsAJobOnTheDownloadQueue)
{
    Fixture f;
    f.serve();
    auto finished = std::make_shared<std::optional<DownloadJob>>();
    auto mutex = std::make_shared<std::mutex>();
    DownloadService service(f.client);
    service.setOnJobFinished([finished, mutex](DownloadJob job) {
        std::lock_guard<std::mutex> lock(*mutex);
        *finished = std::move(job);
    });

    service.enqueueRunner(kArchive,
                          f.archive.entry.rawName,
                          "sample.txt",
                          kDestination,
                          f.archive.entry.compressedSize,
                          makeRunner(f));

    ASSERT_TRUE(eventually([&] {
        std::lock_guard<std::mutex> lock(*mutex);
        return finished->has_value();
    }));
    std::lock_guard<std::mutex> lock(*mutex);
    EXPECT_EQ((*finished)->state, DownloadState::Completed);
    EXPECT_EQ((*finished)->resolvedLocalPath, kDestination);
    EXPECT_EQ((*finished)->subPath, f.archive.entry.rawName);
    EXPECT_FALSE(service.hasJobForHandle(kArchive, f.archive.entry.rawName));
}
