#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Reads a zip's central directory (docs/investigations/STUDY_ARCHIVE_PREVIEW.md
// section 2.1). Listing only: nothing here decompresses anything.
//
// Names come back as the raw bytes stored in the archive -- picking between UTF-8
// and CP932 needs Qt's codecs, which MegaExplorerCore does not link, so the decode
// lives in the QML layer.

struct ZipEntry
{
    std::string rawName;
    std::uint64_t compressedSize = 0;
    std::uint64_t uncompressedSize = 0;
    bool isDirectory = false;
    bool encrypted = false;
    bool nameIsUtf8 = false; // general-purpose flag bit 11
};

// Where the central directory sits, as read out of the End Of Central Directory
// record (plus its ZIP64 counterpart when the 32-bit fields are saturated).
struct ZipDirectoryLocation
{
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

// tail is the last tailOffset..fileSize bytes of the archive; tailOffset is where
// that slice starts within the file. Returns nothing when no valid EOCD is in it.
std::optional<ZipDirectoryLocation> findZipDirectory(const std::vector<char>& tail,
                                                     std::uint64_t tailOffset);

// The bytes of the central directory itself. Stops at the first malformed header
// rather than failing, so a truncated read still lists what it could parse.
std::vector<ZipEntry> parseZipDirectory(const std::vector<char>& directory);

// Tail slice that is always enough to hold an EOCD: 22 fixed bytes plus the maximum
// 65535-byte archive comment, plus the 20-byte ZIP64 locator that sits *ahead* of
// the EOCD -- the study's 65557 stops one record short of it.
constexpr std::uint64_t kZipTailScanBytes = 22 + 65535 + 20;
