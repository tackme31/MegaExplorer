#include "ZipListing.h"

#include <cstddef>

namespace
{

constexpr std::uint32_t kEocdSignature = 0x06054b50;
constexpr std::uint32_t kZip64EocdSignature = 0x06064b50;
constexpr std::uint32_t kZip64LocatorSignature = 0x07064b50;
constexpr std::uint32_t kCentralHeaderSignature = 0x02014b50;

constexpr std::size_t kEocdFixedSize = 22;
constexpr std::size_t kZip64EocdFixedSize = 56;
constexpr std::size_t kZip64LocatorSize = 20;
constexpr std::size_t kCentralHeaderFixedSize = 46;

bool fits(const std::vector<char>& bytes, std::size_t at, std::size_t count)
{
    return at <= bytes.size() && bytes.size() - at >= count;
}

std::uint64_t readLe(const std::vector<char>& bytes, std::size_t at, std::size_t width)
{
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i)
    {
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[at + i])) << (8 * i);
    }
    return value;
}

std::uint16_t readU16(const std::vector<char>& bytes, std::size_t at)
{
    return static_cast<std::uint16_t>(readLe(bytes, at, 2));
}

std::uint32_t readU32(const std::vector<char>& bytes, std::size_t at)
{
    return static_cast<std::uint32_t>(readLe(bytes, at, 4));
}

std::uint64_t readU64(const std::vector<char>& bytes, std::size_t at)
{
    return readLe(bytes, at, 8);
}

// The ZIP64 EOCD, as an index into tail. Looked up even when the 32-bit fields are
// not saturated: writers may emit the record anyway, and missing it would make the
// two records it adds look like prepended data to directoryStart().
std::optional<std::size_t>
findZip64Eocd(const std::vector<char>& tail, std::uint64_t tailOffset, std::size_t eocdAt)
{
    if (eocdAt < kZip64LocatorSize ||
        readU32(tail, eocdAt - kZip64LocatorSize) != kZip64LocatorSignature)
    {
        return std::nullopt;
    }

    // The record normally sits immediately ahead of the locator. Trying that before
    // the locator's pointer is what keeps prepended data working: the pointer is
    // written relative to the archive's own start, not to the file's.
    constexpr std::size_t kBothRecords = kZip64LocatorSize + kZip64EocdFixedSize;
    if (eocdAt >= kBothRecords && readU32(tail, eocdAt - kBothRecords) == kZip64EocdSignature)
        return eocdAt - kBothRecords;

    const std::uint64_t pointer = readU64(tail, eocdAt - kZip64LocatorSize + 8);
    if (pointer < tailOffset)
        return std::nullopt;
    const auto at = static_cast<std::size_t>(pointer - tailOffset);
    if (!fits(tail, at, kZip64EocdFixedSize) || readU32(tail, at) != kZip64EocdSignature)
        return std::nullopt;
    return at;
}

// Prepended data -- a self-extracting stub, or anything else concatenated in front
// of the archive -- shifts every recorded offset by a constant. The position of the
// record that follows the directory is the one thing known for certain, so the real
// start falls out of it. An empty directory has nothing to subtract from.
std::uint64_t directoryStart(std::uint64_t recordAt, std::uint64_t size, std::uint64_t storedOffset)
{
    if (size == 0 || recordAt < size)
        return storedOffset;
    return recordAt - size;
}

// The entry count caps how large the directory can honestly be: a central header
// is 46 bytes plus three fields whose 16-bit lengths sit in it. Without this a
// crafted 22-byte tail could name the whole file as its directory, and the caller
// would fetch that as one byte range. A count saturated at 0xFFFF with no ZIP64
// record to replace it says nothing, so it is not held against the archive.
bool sizeFitsEntryCount(std::uint64_t directorySize, std::uint64_t entries, bool fromZip64)
{
    constexpr std::uint64_t kMaxCentralHeaderSize = kCentralHeaderFixedSize + 3 * 65535;
    if (!fromZip64 && entries == 0xFFFFu)
        return true;
    return directorySize / kMaxCentralHeaderSize <= entries;
}

// Rejects a candidate whose directory cannot be where it claims: it has to end at
// or before the record that describes it, and -- when the tail slice happens to
// cover its start, which is the common case of a small archive -- it has to begin
// with a central header. Together these throw out a stray EOCD signature inside
// stored data or inside the archive comment.
bool startIsPlausible(const std::vector<char>& tail,
                      std::uint64_t tailOffset,
                      std::uint64_t recordAt,
                      const ZipDirectoryLocation& location)
{
    if (location.size > recordAt || location.offset > recordAt - location.size)
        return false;
    if (location.size == 0 || location.offset < tailOffset)
        return true;
    const std::uint64_t relative = location.offset - tailOffset;
    if (relative > tail.size())
        return true;
    const auto at = static_cast<std::size_t>(relative);
    if (!fits(tail, at, 4))
        return true;
    return readU32(tail, at) == kCentralHeaderSignature;
}

// Scans the extra field for one record id, returning its payload span.
bool findExtraField(const std::vector<char>& bytes,
                    std::size_t extraAt,
                    std::size_t extraLength,
                    std::uint16_t wantedId,
                    std::size_t& payloadAt,
                    std::size_t& payloadLength)
{
    std::size_t at = extraAt;
    const std::size_t end = extraAt + extraLength;
    while (at + 4 <= end)
    {
        const std::uint16_t id = readU16(bytes, at);
        const std::size_t length = readU16(bytes, at + 2);
        if (at + 4 + length > end)
            return false;
        if (id == wantedId)
        {
            payloadAt = at + 4;
            payloadLength = length;
            return true;
        }
        at += 4 + length;
    }
    return false;
}

} // namespace

std::optional<ZipDirectoryLocation> findZipDirectory(const std::vector<char>& tail,
                                                     std::uint64_t tailOffset)
{
    if (tail.size() < kEocdFixedSize)
        return std::nullopt;

    for (std::size_t back = 0; back + kEocdFixedSize <= tail.size(); ++back)
    {
        const std::size_t at = tail.size() - kEocdFixedSize - back;
        if (readU32(tail, at) != kEocdSignature)
            continue;
        // A stored file, or the archive comment, can hold these four bytes by
        // chance; only the comment length agreeing with what is actually left
        // settles it.
        if (readU16(tail, at + 20) != tail.size() - at - kEocdFixedSize)
            continue;

        std::uint64_t size = readU32(tail, at + 12);
        std::uint64_t offset = readU32(tail, at + 16);
        std::uint64_t entries = readU16(tail, at + 10);
        std::uint64_t recordAt = tailOffset + at;
        bool fromZip64 = false;
        if (const std::optional<std::size_t> zip64At = findZip64Eocd(tail, tailOffset, at))
        {
            entries = readU64(tail, *zip64At + 32);
            size = readU64(tail, *zip64At + 40);
            offset = readU64(tail, *zip64At + 48);
            recordAt = tailOffset + *zip64At;
            fromZip64 = true;
        }
        if (!sizeFitsEntryCount(size, entries, fromZip64))
            continue;

        ZipDirectoryLocation location;
        location.size = size;
        location.offset = directoryStart(recordAt, size, offset);
        if (!startIsPlausible(tail, tailOffset, recordAt, location))
        {
            // Something other than prepended data separates the directory from the
            // record; the stored offset is the only other candidate.
            location.offset = offset;
            if (!startIsPlausible(tail, tailOffset, recordAt, location))
                continue;
        }
        return location;
    }
    return std::nullopt;
}

std::vector<ZipEntry> parseZipDirectory(const std::vector<char>& directory)
{
    std::vector<ZipEntry> entries;
    std::size_t at = 0;
    while (fits(directory, at, kCentralHeaderFixedSize) &&
           readU32(directory, at) == kCentralHeaderSignature)
    {
        const std::uint16_t flags = readU16(directory, at + 8);
        const std::size_t nameLength = readU16(directory, at + 28);
        const std::size_t extraLength = readU16(directory, at + 30);
        const std::size_t commentLength = readU16(directory, at + 32);
        if (!fits(
                directory, at + kCentralHeaderFixedSize, nameLength + extraLength + commentLength))
        {
            break;
        }

        ZipEntry entry;
        entry.compressedSize = readU32(directory, at + 20);
        entry.uncompressedSize = readU32(directory, at + 24);
        entry.encrypted = (flags & 0x0001) != 0;
        entry.nameIsUtf8 = (flags & 0x0800) != 0;
        entry.rawName.assign(directory.data() + at + kCentralHeaderFixedSize, nameLength);

        const std::size_t extraAt = at + kCentralHeaderFixedSize + nameLength;
        std::size_t payloadAt = 0;
        std::size_t payloadLength = 0;
        if ((entry.compressedSize == 0xFFFFFFFFu || entry.uncompressedSize == 0xFFFFFFFFu) &&
            findExtraField(directory, extraAt, extraLength, 0x0001, payloadAt, payloadLength))
        {
            // Only the saturated fields are present, in a fixed order, with the rest
            // closed up -- so which one comes first depends on which saturated.
            std::size_t cursor = payloadAt;
            const std::size_t end = payloadAt + payloadLength;
            if (entry.uncompressedSize == 0xFFFFFFFFu && cursor + 8 <= end)
            {
                entry.uncompressedSize = readU64(directory, cursor);
                cursor += 8;
            }
            if (entry.compressedSize == 0xFFFFFFFFu && cursor + 8 <= end)
            {
                entry.compressedSize = readU64(directory, cursor);
            }
        }
        // Info-ZIP Unicode Path: version(1) + CRC32 of the original name(4) + UTF-8
        // name. Present exactly when the stored name is not UTF-8, which is the
        // common Windows/Shift-JIS case.
        if (findExtraField(directory, extraAt, extraLength, 0x7075, payloadAt, payloadLength) &&
            payloadLength > 5)
        {
            entry.rawName.assign(directory.data() + payloadAt + 5, payloadLength - 5);
            entry.nameIsUtf8 = true;
        }

        entry.isDirectory = !entry.rawName.empty() && entry.rawName.back() == '/';
        entries.push_back(std::move(entry));

        at += kCentralHeaderFixedSize + nameLength + extraLength + commentLength;
    }
    return entries;
}
