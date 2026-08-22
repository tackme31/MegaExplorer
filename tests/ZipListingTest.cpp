#include "core/ZipListing.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <vector>

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

void putU64(std::vector<char>& out, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
}

void putText(std::vector<char>& out, const std::string& text)
{
    out.insert(out.end(), text.begin(), text.end());
}

void append(std::vector<char>& out, const std::vector<char>& more)
{
    out.insert(out.end(), more.begin(), more.end());
}

std::string asText(const std::vector<char>& bytes)
{
    return std::string(bytes.begin(), bytes.end());
}

struct Entry
{
    std::string name;
    std::uint32_t compressed = 0;
    std::uint32_t uncompressed = 0;
    std::uint16_t flags = 0;
    std::string extra;
    std::string comment;
};

std::vector<char> centralHeader(const Entry& entry)
{
    std::vector<char> out;
    putU32(out, 0x02014b50);
    putU16(out, 20); // version made by
    putU16(out, 20); // version needed
    putU16(out, entry.flags);
    putU16(out, 8); // deflate
    putU16(out, 0); // modified time
    putU16(out, 0); // modified date
    putU32(out, 0); // crc-32
    putU32(out, entry.compressed);
    putU32(out, entry.uncompressed);
    putU16(out, static_cast<std::uint16_t>(entry.name.size()));
    putU16(out, static_cast<std::uint16_t>(entry.extra.size()));
    putU16(out, static_cast<std::uint16_t>(entry.comment.size()));
    putU16(out, 0); // disk number start
    putU16(out, 0); // internal attributes
    putU32(out, 0); // external attributes
    putU32(out, 0); // local header offset
    putText(out, entry.name);
    putText(out, entry.extra);
    putText(out, entry.comment);
    return out;
}

std::vector<char> eocd(std::uint16_t entries,
                       std::uint32_t directorySize,
                       std::uint32_t directoryOffset,
                       const std::string& comment = {})
{
    std::vector<char> out;
    putU32(out, 0x06054b50);
    putU16(out, 0); // this disk
    putU16(out, 0); // disk the directory starts on
    putU16(out, entries);
    putU16(out, entries);
    putU32(out, directorySize);
    putU32(out, directoryOffset);
    putU16(out, static_cast<std::uint16_t>(comment.size()));
    putText(out, comment);
    return out;
}

std::vector<char>
zip64Eocd(std::uint64_t entries, std::uint64_t directorySize, std::uint64_t directoryOffset)
{
    std::vector<char> out;
    putU32(out, 0x06064b50);
    putU64(out, 44); // size of the remainder of this record
    putU16(out, 45); // version made by
    putU16(out, 45); // version needed
    putU32(out, 0);  // this disk
    putU32(out, 0);  // disk the directory starts on
    putU64(out, entries);
    putU64(out, entries);
    putU64(out, directorySize);
    putU64(out, directoryOffset);
    return out;
}

std::vector<char> zip64Locator(std::uint64_t zip64EocdOffset)
{
    std::vector<char> out;
    putU32(out, 0x07064b50);
    putU32(out, 0); // disk the ZIP64 EOCD is on
    putU64(out, zip64EocdOffset);
    putU32(out, 1); // total disks
    return out;
}

std::vector<char> directoryBytes(const std::vector<char>& file,
                                 const ZipDirectoryLocation& location)
{
    const auto at = static_cast<std::ptrdiff_t>(location.offset);
    const auto size = static_cast<std::ptrdiff_t>(location.size);
    return std::vector<char>(file.begin() + at, file.begin() + at + size);
}

// A two-entry archive with `prefix` bytes of junk in front of everything. The
// offsets it records are relative to the archive, not to the file, which is what a
// self-extracting stub produces.
std::vector<char> buildArchive(std::size_t prefix, std::size_t localData, std::size_t& directoryAt)
{
    std::vector<char> directory;
    append(directory, centralHeader({"readme.txt", 40, 90, 0x0800, "", ""}));
    append(directory, centralHeader({"docs/", 0, 0, 0, "", ""}));

    std::vector<char> file(prefix + localData, 'x');
    directoryAt = file.size();
    append(file, directory);
    append(file,
           eocd(2,
                static_cast<std::uint32_t>(directory.size()),
                static_cast<std::uint32_t>(localData)));
    return file;
}

} // namespace

TEST(ZipListingTest, FindsTheDirectoryOfAPlainArchive)
{
    std::size_t directoryAt = 0;
    const std::vector<char> file = buildArchive(0, 100, directoryAt);

    const auto location = findZipDirectory(file, 0);
    ASSERT_TRUE(location.has_value());
    EXPECT_EQ(location->offset, directoryAt);
    EXPECT_EQ(location->size, file.size() - directoryAt - 22);
}

TEST(ZipListingTest, ReadsNameSizeAndDirectoryFlagOfEachEntry)
{
    std::size_t directoryAt = 0;
    const std::vector<char> file = buildArchive(0, 100, directoryAt);
    const auto location = findZipDirectory(file, 0);
    ASSERT_TRUE(location.has_value());

    const std::vector<ZipEntry> entries = parseZipDirectory(directoryBytes(file, *location));
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].rawName, "readme.txt");
    EXPECT_EQ(entries[0].compressedSize, 40u);
    EXPECT_EQ(entries[0].uncompressedSize, 90u);
    EXPECT_FALSE(entries[0].isDirectory);
    EXPECT_TRUE(entries[0].nameIsUtf8);
    EXPECT_FALSE(entries[0].encrypted);
    EXPECT_EQ(entries[1].rawName, "docs/");
    EXPECT_TRUE(entries[1].isDirectory);
    EXPECT_FALSE(entries[1].nameIsUtf8);
}

TEST(ZipListingTest, CorrectsOffsetsForPrependedData)
{
    std::size_t directoryAt = 0;
    const std::vector<char> file = buildArchive(500, 100, directoryAt);

    const auto location = findZipDirectory(file, 0);
    ASSERT_TRUE(location.has_value());
    EXPECT_EQ(location->offset, 600u);
    EXPECT_EQ(location->offset, directoryAt);
    EXPECT_EQ(parseZipDirectory(directoryBytes(file, *location)).size(), 2u);
}

TEST(ZipListingTest, IgnoresAnEocdSignatureHiddenInStoredData)
{
    std::vector<char> directory;
    append(directory, centralHeader({"a.txt", 1, 1, 0, "", ""}));

    // A stored file whose bytes happen to spell an EOCD, with a comment length that
    // does not match what follows it.
    std::vector<char> file(20, 'x');
    append(file, eocd(9, 999, 999));
    file.resize(200, 'y');

    const auto directoryAt = static_cast<std::uint32_t>(file.size());
    append(file, directory);
    append(file, eocd(1, static_cast<std::uint32_t>(directory.size()), directoryAt));

    const auto location = findZipDirectory(file, 0);
    ASSERT_TRUE(location.has_value());
    EXPECT_EQ(location->offset, directoryAt);
    ASSERT_EQ(parseZipDirectory(directoryBytes(file, *location)).size(), 1u);
}

TEST(ZipListingTest, IgnoresAnEocdSignatureHiddenInTheArchiveComment)
{
    std::vector<char> directory;
    append(directory, centralHeader({"a.txt", 1, 1, 0, "", ""}));

    // The decoy sits 4 bytes into a 40-byte comment, so its own comment-length
    // field can be made to agree with the 14 bytes left after it -- the check that
    // weeds out a stray signature elsewhere. Only its impossible directory
    // location rules it out.
    std::vector<char> comment(4, 'z');
    append(comment, eocd(1, 0x10000000, 0x20000000, std::string(14, 'z')));
    ASSERT_EQ(comment.size(), 40u);

    std::vector<char> file(100, 'x');
    const auto directoryAt = static_cast<std::uint32_t>(file.size());
    append(file, directory);
    append(file,
           eocd(1, static_cast<std::uint32_t>(directory.size()), directoryAt, asText(comment)));

    const auto location = findZipDirectory(file, 0);
    ASSERT_TRUE(location.has_value());
    EXPECT_EQ(location->offset, directoryAt);
    EXPECT_EQ(location->size, directory.size());
}

TEST(ZipListingTest, ReadsTheZip64RecordsWhenTheThirtyTwoBitFieldsAreSaturated)
{
    std::vector<char> extra;
    putU16(extra, 0x0001);
    putU16(extra, 16);
    putU64(extra, 5000000000ull); // uncompressed
    putU64(extra, 4000000000ull); // compressed

    std::vector<char> directory;
    append(directory, centralHeader({"huge.bin", 0xFFFFFFFFu, 0xFFFFFFFFu, 0, asText(extra), ""}));

    std::vector<char> file(100, 'x');
    const auto directoryAt = static_cast<std::uint64_t>(file.size());
    append(file, directory);
    const auto zip64At = static_cast<std::uint64_t>(file.size());
    append(file, zip64Eocd(1, directory.size(), directoryAt));
    append(file, zip64Locator(zip64At));
    append(file, eocd(0xFFFF, 0xFFFFFFFFu, 0xFFFFFFFFu));

    const auto location = findZipDirectory(file, 0);
    ASSERT_TRUE(location.has_value());
    EXPECT_EQ(location->offset, directoryAt);
    EXPECT_EQ(location->size, directory.size());

    const std::vector<ZipEntry> entries = parseZipDirectory(directoryBytes(file, *location));
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].uncompressedSize, 5000000000ull);
    EXPECT_EQ(entries[0].compressedSize, 4000000000ull);
}

TEST(ZipListingTest, DoesNotMistakeUnsaturatedZip64RecordsForPrependedData)
{
    std::vector<char> directory;
    append(directory, centralHeader({"small.txt", 3, 5, 0, "", ""}));

    std::vector<char> file(100, 'x');
    const auto directoryAt = static_cast<std::uint64_t>(file.size());
    append(file, directory);
    const auto zip64At = static_cast<std::uint64_t>(file.size());
    append(file, zip64Eocd(1, directory.size(), directoryAt));
    append(file, zip64Locator(zip64At));
    append(file,
           eocd(1,
                static_cast<std::uint32_t>(directory.size()),
                static_cast<std::uint32_t>(directoryAt)));

    const auto location = findZipDirectory(file, 0);
    ASSERT_TRUE(location.has_value());
    EXPECT_EQ(location->offset, directoryAt);
}

TEST(ZipListingTest, WorksOnATailSliceThatDoesNotReachTheDirectory)
{
    std::size_t directoryAt = 0;
    const std::vector<char> file = buildArchive(0, 100, directoryAt);
    ASSERT_GT(file.size(), 60u);

    const std::uint64_t tailOffset = file.size() - 60;
    const std::vector<char> tail(file.begin() + static_cast<std::ptrdiff_t>(tailOffset),
                                 file.end());

    const auto location = findZipDirectory(tail, tailOffset);
    ASSERT_TRUE(location.has_value());
    EXPECT_EQ(location->offset, directoryAt);
}

TEST(ZipListingTest, PrefersTheInfoZipUnicodePathOverTheStoredName)
{
    const std::string utf8Name = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e.txt";
    std::vector<char> extra;
    putU16(extra, 0x7075);
    putU16(extra, static_cast<std::uint16_t>(5 + utf8Name.size()));
    extra.push_back(1);         // version
    putU32(extra, 0x12345678u); // crc of the stored name
    putText(extra, utf8Name);

    std::vector<char> directory;
    append(directory, centralHeader({"mojibake.txt", 1, 1, 0, asText(extra), ""}));

    const std::vector<ZipEntry> entries = parseZipDirectory(directory);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].rawName, utf8Name);
    EXPECT_TRUE(entries[0].nameIsUtf8);
}

TEST(ZipListingTest, ReportsEncryptedEntries)
{
    std::vector<char> directory;
    append(directory, centralHeader({"secret.txt", 1, 1, 0x0001, "", ""}));

    const std::vector<ZipEntry> entries = parseZipDirectory(directory);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_TRUE(entries[0].encrypted);
    EXPECT_EQ(entries[0].rawName, "secret.txt");
}

TEST(ZipListingTest, ListsWhatItCanFromATruncatedDirectory)
{
    std::vector<char> directory;
    append(directory, centralHeader({"first.txt", 1, 1, 0, "", ""}));
    append(directory, centralHeader({"second.txt", 1, 1, 0, "", ""}));
    directory.resize(directory.size() - 10);

    const std::vector<ZipEntry> entries = parseZipDirectory(directory);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].rawName, "first.txt");
}

TEST(ZipListingTest, HandlesAnEmptyArchive)
{
    const std::vector<char> file = eocd(0, 0, 0);

    const auto location = findZipDirectory(file, 0);
    ASSERT_TRUE(location.has_value());
    EXPECT_EQ(location->size, 0u);
    EXPECT_TRUE(parseZipDirectory({}).empty());
}

TEST(ZipListingTest, ReturnsNothingWhenThereIsNoEocd)
{
    EXPECT_FALSE(findZipDirectory(std::vector<char>(200, 'x'), 0).has_value());
    EXPECT_FALSE(findZipDirectory({}, 0).has_value());
}

TEST(ZipListingTest, RejectsADirectorySizeTheEntryCountCannotAccountFor)
{
    // On a tail slice the directory's own first bytes are out of reach, so the
    // entry count is all that stops a crafted record from naming the whole file.
    EXPECT_FALSE(findZipDirectory(eocd(1, 400000, 0), 500000).has_value());

    const auto location = findZipDirectory(eocd(10, 400000, 0), 500000);
    ASSERT_TRUE(location.has_value());
    EXPECT_EQ(location->size, 400000u);
    EXPECT_EQ(location->offset, 100000u);
}
