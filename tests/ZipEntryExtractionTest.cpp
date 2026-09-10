#include "core/ZipEntryExtraction.h"

#include "core/ILocalFileSystem.h"
#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"

#include <algorithm>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <optional>
#include <string>
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

class MemoryFileSystem : public ILocalFileSystem
{
public:
    std::map<std::string, std::string> committed;
    int liveWriters = 0;
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
    int chunksServed = 0;
    std::optional<Result<void>> failAfterFirstChunk;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> streamed;

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
                length = std::min<std::uint64_t>(length, archive.bytes.size() - offset);
                const char* data = archive.bytes.data() + offset;
                for (std::uint64_t at = 0; at < length; at += chunk)
                {
                    ++chunksServed;
                    const auto size =
                        static_cast<std::size_t>(std::min<std::uint64_t>(chunk, length - at));
                    if (!onChunk(data + at, size))
                    {
                        onDone(Result<void>::fail("aborted", MegaErrorCode::kEIncomplete));
                        return;
                    }
                    if (failAfterFirstChunk)
                    {
                        onDone(*failAfterFirstChunk);
                        return;
                    }
                }
                onDone(Result<void>::ok());
            });
    }

    std::unique_ptr<ZipEntryExtraction> make()
    {
        return std::make_unique<ZipEntryExtraction>(
            client, fs, kArchive, archive.entry, kStubSize, kDestination);
    }
};

Result<void> run(ZipEntryExtraction& extraction, const ZipEntryExtraction::Progress& progress = {})
{
    std::optional<Result<void>> outcome;
    extraction.start(progress, [&outcome](Result<void> r) {
        outcome = std::move(r);
    });
    EXPECT_TRUE(outcome.has_value());
    return outcome.value_or(Result<void>::fail("never finished", 0));
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
    EXPECT_EQ(f.fs->liveWriters, 0);
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
    EXPECT_EQ(f.fs->liveWriters, 0);
}

TEST(ZipEntryExtractionTest, CancelStopsTheTransferAtTheNextChunk)
{
    Fixture f;
    f.serve();
    const auto extraction = f.make();

    const Result<void> result = run(*extraction, [&](std::uint64_t, std::uint64_t) {
        extraction->cancel();
    });

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, MegaErrorCode::kEIncomplete);
    EXPECT_EQ(f.chunksServed, 2); // the one that reported progress, and the one refused
    EXPECT_TRUE(f.fs->committed.empty());
    EXPECT_EQ(f.fs->liveWriters, 0);
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
    EXPECT_EQ(f.fs->liveWriters, 0);
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
    EXPECT_EQ(f.fs->liveWriters, 0);
}

TEST(ZipEntryExtractionTest, AWriteFailureStopsTheTransfer)
{
    Fixture f;
    f.fs->failWrites = true;
    f.serve();
    const auto extraction = f.make();

    const Result<void> result = run(*extraction);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, MegaErrorCode::kEFailed);
    EXPECT_EQ(f.chunksServed, 1);
    EXPECT_TRUE(f.fs->committed.empty());
    EXPECT_EQ(f.fs->liveWriters, 0);
}
