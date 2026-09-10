#include "core/ZipExtract.h"

#include <algorithm>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <vector>
#include <zlib.h>

namespace
{

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

std::vector<char> localHeader(const std::string& name, std::size_t extraLength)
{
    std::vector<char> out;
    putU32(out, 0x04034b50);
    putU16(out, 20); // version needed
    putU16(out, 0);  // flags
    putU16(out, 8);  // deflate
    putU32(out, 0);  // modified time and date
    putU32(out, 0);  // crc-32
    putU32(out, 0);  // compressed size
    putU32(out, 0);  // uncompressed size
    putU16(out, static_cast<std::uint16_t>(name.size()));
    putU16(out, static_cast<std::uint16_t>(extraLength));
    out.insert(out.end(), name.begin(), name.end());
    out.insert(out.end(), extraLength, 'e');
    return out;
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

std::uint32_t crcOf(const std::string& data)
{
    return static_cast<std::uint32_t>(
        crc32(0L, reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(data.size())));
}

// Compressible, but with enough variety to need several deflate blocks.
std::string sampleText(std::size_t size)
{
    std::string text;
    for (std::size_t i = 0; text.size() < size; ++i)
        text += "line " + std::to_string(i * 7919 % 10007) + " of the sample\n";
    text.resize(size);
    return text;
}

ZipEntry entryFor(const std::string& original, const std::string& compressed, std::uint16_t method)
{
    ZipEntry entry;
    entry.rawName = "sample.txt";
    entry.compressionMethod = method;
    entry.compressedSize = compressed.size();
    entry.uncompressedSize = original.size();
    entry.crc = crcOf(original);
    return entry;
}

struct Extraction
{
    ZipExtractStatus status = ZipExtractStatus::Pending;
    std::string output;
};

Extraction extractInChunks(const ZipEntry& entry, const std::string& compressed, std::size_t chunk)
{
    Extraction run;
    ZipEntryInflater inflater(entry, [&run](const char* data, std::size_t size) {
        run.output.append(data, size);
        return true;
    });
    for (std::size_t at = 0; at < compressed.size(); at += chunk)
    {
        if (!inflater.feed(compressed.data() + at, (std::min)(chunk, compressed.size() - at)))
            break;
    }
    run.status = inflater.finish();
    return run;
}

} // namespace

TEST(ZipExtractTest, FindsTheDataAfterTheLocalHeadersOwnNameAndExtra)
{
    // The central directory's extra length for the same entry would differ; only
    // the local header's counts.
    const std::vector<char> header = localHeader("dir/sample.txt", 28);

    const auto dataAt = zipEntryDataOffset(header, 1000);
    ASSERT_TRUE(dataAt.has_value());
    EXPECT_EQ(*dataAt, 1000u + 30u + 14u + 28u);
}

TEST(ZipExtractTest, RejectsBytesThatAreNotALocalHeader)
{
    std::vector<char> header = localHeader("a.txt", 0);
    header[0] = 'x';
    EXPECT_FALSE(zipEntryDataOffset(header, 0).has_value());

    const std::vector<char> shortHeader(29, '\0');
    EXPECT_FALSE(zipEntryDataOffset(shortHeader, 0).has_value());

    const std::vector<char> valid = localHeader("a.txt", 0);
    EXPECT_FALSE(zipEntryDataOffset(valid, ~std::uint64_t{0} - 10).has_value());
}

TEST(ZipExtractTest, InflatesDeflateDataFedInArbitraryChunks)
{
    const std::string original = sampleText(300000);
    const std::string compressed = rawDeflate(original);
    const ZipEntry entry = entryFor(original, compressed, 8);

    for (const std::size_t chunk :
         {std::size_t{1}, std::size_t{7}, std::size_t{4096}, std::size_t{65536}, compressed.size()})
    {
        SCOPED_TRACE(chunk);
        const Extraction run = extractInChunks(entry, compressed, chunk);
        EXPECT_EQ(run.status, ZipExtractStatus::Ok);
        EXPECT_EQ(run.output, original);
    }
}

TEST(ZipExtractTest, PassesStoredDataThroughUnchanged)
{
    const std::string original = sampleText(5000);
    const Extraction run = extractInChunks(entryFor(original, original, 0), original, 13);
    EXPECT_EQ(run.status, ZipExtractStatus::Ok);
    EXPECT_EQ(run.output, original);
}

TEST(ZipExtractTest, ExtractsEmptyEntries)
{
    const Extraction stored = extractInChunks(entryFor("", "", 0), "", 1);
    EXPECT_EQ(stored.status, ZipExtractStatus::Ok);

    const std::string compressed = rawDeflate("");
    const Extraction deflated = extractInChunks(entryFor("", compressed, 8), compressed, 1);
    EXPECT_EQ(deflated.status, ZipExtractStatus::Ok);
    EXPECT_TRUE(deflated.output.empty());

    const Extraction bare = extractInChunks(entryFor("", "", 8), "", 1);
    EXPECT_EQ(bare.status, ZipExtractStatus::Ok);
}

TEST(ZipExtractTest, ReportsACrcMismatch)
{
    const std::string original = sampleText(10000);
    const std::string compressed = rawDeflate(original);
    ZipEntry entry = entryFor(original, compressed, 8);
    entry.crc ^= 1;

    EXPECT_EQ(extractInChunks(entry, compressed, 4096).status, ZipExtractStatus::CrcMismatch);
}

TEST(ZipExtractTest, ReportsCorruptDeflateData)
{
    // Block type 3 is reserved, so this fails on the first byte.
    const std::string garbage(64, '\xFF');
    const ZipEntry entry = entryFor(std::string(100, 'a'), garbage, 8);

    EXPECT_EQ(extractInChunks(entry, garbage, 16).status, ZipExtractStatus::CorruptData);
}

TEST(ZipExtractTest, ReportsAStreamCutShort)
{
    const std::string original = sampleText(50000);
    const std::string compressed = rawDeflate(original);
    const ZipEntry entry = entryFor(original, compressed, 8);

    ZipEntryInflater inflater(entry, [](const char*, std::size_t) {
        return true;
    });
    ASSERT_TRUE(inflater.feed(compressed.data(), compressed.size() / 2));
    EXPECT_EQ(inflater.finish(), ZipExtractStatus::Truncated);
}

TEST(ZipExtractTest, NeverWritesMoreThanTheDirectoryDeclares)
{
    const std::string original = sampleText(200000);
    const std::string compressed = rawDeflate(original);
    ZipEntry entry = entryFor(original, compressed, 8);
    entry.uncompressedSize = 1000;

    const Extraction run = extractInChunks(entry, compressed, 4096);
    EXPECT_EQ(run.status, ZipExtractStatus::SizeMismatch);
    EXPECT_LE(run.output.size(), 1000u);
}

TEST(ZipExtractTest, RejectsBytesPastTheEndOfTheDeflateStream)
{
    const std::string original = sampleText(1000);
    const std::string compressed = rawDeflate(original) + "trailing";
    const ZipEntry entry = entryFor(original, compressed, 8);

    EXPECT_EQ(extractInChunks(entry, compressed, 1).status, ZipExtractStatus::CorruptData);
    EXPECT_EQ(extractInChunks(entry, compressed, compressed.size()).status,
              ZipExtractStatus::CorruptData);
}

TEST(ZipExtractTest, RefusesMoreCompressedBytesThanTheDirectoryDeclares)
{
    const std::string original = sampleText(100);
    const std::string fed = original + "x";

    ZipEntryInflater inflater(entryFor(original, original, 0), [](const char*, std::size_t) {
        return true;
    });
    EXPECT_FALSE(inflater.feed(fed.data(), fed.size()));
    EXPECT_EQ(inflater.status(), ZipExtractStatus::SizeMismatch);
}

TEST(ZipExtractTest, StopsWhenTheSinkRefuses)
{
    const std::string original = sampleText(300000);
    const std::string compressed = rawDeflate(original);

    int calls = 0;
    ZipEntryInflater inflater(entryFor(original, compressed, 8),
                              [&calls](const char*, std::size_t) {
                                  ++calls;
                                  return false;
                              });
    EXPECT_FALSE(inflater.feed(compressed.data(), compressed.size()));
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(inflater.finish(), ZipExtractStatus::SinkFailed);
}

TEST(ZipExtractTest, ClassifiesWhatItCannotExtract)
{
    ZipEntry entry;
    entry.compressionMethod = 0;
    EXPECT_EQ(zipEntrySupport(entry), ZipEntrySupport::Supported);
    entry.compressionMethod = 8;
    EXPECT_EQ(zipEntrySupport(entry), ZipEntrySupport::Supported);

    for (const std::uint16_t method : std::initializer_list<std::uint16_t>{9, 12, 14, 93})
    {
        entry.compressionMethod = method;
        EXPECT_EQ(zipEntrySupport(entry), ZipEntrySupport::UnsupportedMethod) << method;
    }

    // WinZip AES sets both the encrypted flag and method 99.
    entry.compressionMethod = 99;
    entry.encrypted = true;
    EXPECT_EQ(zipEntrySupport(entry), ZipEntrySupport::Encrypted);
    entry.compressionMethod = 8;
    EXPECT_EQ(zipEntrySupport(entry), ZipEntrySupport::Encrypted);

    ZipEntry folder;
    folder.rawName = "docs/";
    folder.isDirectory = true;
    EXPECT_EQ(zipEntrySupport(folder), ZipEntrySupport::Directory);
}

TEST(ZipExtractTest, RefusesToInflateAnUnsupportedEntry)
{
    ZipEntry entry;
    entry.compressionMethod = 14;
    entry.compressedSize = 4;
    entry.uncompressedSize = 4;

    int calls = 0;
    ZipEntryInflater inflater(entry, [&calls](const char*, std::size_t) {
        ++calls;
        return true;
    });
    EXPECT_FALSE(inflater.feed("abcd", 4));
    EXPECT_EQ(inflater.finish(), ZipExtractStatus::Unsupported);
    EXPECT_EQ(calls, 0);
}
