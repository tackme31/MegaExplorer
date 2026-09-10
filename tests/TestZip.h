#pragma once
#include <cstdint>
#include <string>
#include <vector>

// A central directory and its EOCD, with filler where the local headers would be:
// the listing path never reads those. For the tests of what a listing is folded
// into (PreviewControllerTest, ViewerControllerTest); ZipListingTest.cpp builds its
// own, because malformed records are what it exercises.
namespace testzip
{

inline void putU16(std::vector<char>& out, std::uint16_t value)
{
    out.push_back(static_cast<char>(value & 0xFF));
    out.push_back(static_cast<char>((value >> 8) & 0xFF));
}

inline void putU32(std::vector<char>& out, std::uint32_t value)
{
    putU16(out, static_cast<std::uint16_t>(value & 0xFFFF));
    putU16(out, static_cast<std::uint16_t>((value >> 16) & 0xFFFF));
}

struct EntrySpec
{
    std::string name;
    std::uint32_t uncompressed = 0;
    bool utf8 = false;
};

inline std::vector<char> buildZip(const std::vector<EntrySpec>& entries)
{
    std::vector<char> directory;
    for (const EntrySpec& entry : entries)
    {
        putU32(directory, 0x02014b50);
        putU16(directory, 20);                      // version made by
        putU16(directory, 20);                      // version needed
        putU16(directory, entry.utf8 ? 0x0800 : 0); // general purpose flags
        putU16(directory, 8);                       // deflate
        putU32(directory, 0);                       // modified time and date
        putU32(directory, 0);                       // crc-32
        putU32(directory, entry.uncompressed);      // compressed size
        putU32(directory, entry.uncompressed);      // uncompressed size
        putU16(directory, static_cast<std::uint16_t>(entry.name.size()));
        putU16(directory, 0); // extra length
        putU16(directory, 0); // comment length
        putU16(directory, 0); // disk number start
        putU16(directory, 0); // internal attributes
        putU32(directory, 0); // external attributes
        putU32(directory, 0); // local header offset
        directory.insert(directory.end(), entry.name.begin(), entry.name.end());
    }

    std::vector<char> file(64, 'x');
    const auto directoryAt = static_cast<std::uint32_t>(file.size());
    file.insert(file.end(), directory.begin(), directory.end());
    putU32(file, 0x06054b50);
    putU16(file, 0); // this disk
    putU16(file, 0); // disk the directory starts on
    putU16(file, static_cast<std::uint16_t>(entries.size()));
    putU16(file, static_cast<std::uint16_t>(entries.size()));
    putU32(file, static_cast<std::uint32_t>(directory.size()));
    putU32(file, directoryAt);
    putU16(file, 0); // comment length
    return file;
}

} // namespace testzip
