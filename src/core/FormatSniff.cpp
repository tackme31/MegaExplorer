#include "FormatSniff.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <string_view>

using namespace std::string_view_literals;

namespace
{

bool hasAt(const std::vector<char>& head, std::size_t offset, std::string_view signature)
{
    return head.size() >= offset + signature.size() &&
           std::memcmp(head.data() + offset, signature.data(), signature.size()) == 0;
}

std::uint32_t littleEndian32(const std::vector<char>& head, std::size_t offset)
{
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(head[offset + i]))
                 << (8 * i);
    return value;
}

// "BM" alone is two bytes any text could start with, so the DIB header's own size
// field, one of a handful of fixed values, has to agree.
bool isBitmap(const std::vector<char>& head)
{
    if (!hasAt(head, 0, "BM"sv) || head.size() < 18)
        return false;
    switch (littleEndian32(head, 14))
    {
        case 12:
        case 40:
        case 52:
        case 56:
        case 64:
        case 108:
        case 124:
            return true;
        default:
            return false;
    }
}

std::uint32_t bigEndian32(const std::vector<char>& head, std::size_t offset)
{
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i)
        value = (value << 8) | static_cast<unsigned char>(head[offset + i]);
    return value;
}

// Stills use the same box layout as MP4 (HEIF, AVIF, Canon's CR3), and a HEIF's major
// brand need not be one of its own -- so every brand in the ftyp box that was read is
// asked. A sequence brand (animated HEIF/AVIF) could be played as a video, so it proves
// nothing either way.
SniffedFormat isoMediaFormat(const std::vector<char>& head)
{
    static constexpr std::string_view sequenceBrands[] = {
        "msf1"sv, "hevc"sv, "hevx"sv, "hevm"sv, "hevs"sv, "avis"sv, "avcs"sv};
    static constexpr std::string_view stillBrands[] = {
        "mif1"sv, "mif2"sv, "heic"sv, "heix"sv, "heim"sv, "heis"sv, "avif"sv, "avci"sv, "crx "sv};
    const auto listed = [](const auto& brands, std::string_view brand) {
        return std::find(std::begin(brands), std::end(brands), brand) != std::end(brands);
    };

    // Major brand at 8, minor version at 12, compatible brands from 16 to the box's end.
    std::size_t boxEnd = bigEndian32(head, 0);
    boxEnd = std::min(std::max<std::size_t>(boxEnd, 12), head.size());
    bool still = false;
    for (std::size_t offset = 8; offset + 4 <= boxEnd;
         offset += offset == 8 ? std::size_t{8} : std::size_t{4})
    {
        const std::string_view brand(head.data() + offset, 4);
        if (listed(sequenceBrands, brand))
            return SniffedFormat::Unknown;
        still = still || listed(stillBrands, brand);
    }
    return still ? SniffedFormat::Image : SniffedFormat::Media;
}

} // namespace

SniffedFormat sniffFormat(const std::vector<char>& head)
{
    if (hasAt(head, 0, "\x89PNG\r\n\x1a\n"sv) || hasAt(head, 0, "\xFF\xD8\xFF"sv) ||
        hasAt(head, 0, "GIF87a"sv) || hasAt(head, 0, "GIF89a"sv) || hasAt(head, 0, "II*\0"sv) ||
        hasAt(head, 0, "MM\0*"sv) || isBitmap(head))
        return SniffedFormat::Image;

    if (hasAt(head, 0, "RIFF"sv))
    {
        if (hasAt(head, 8, "WEBP"sv))
            return SniffedFormat::Image;
        if (hasAt(head, 8, "AVI "sv) || hasAt(head, 8, "WAVE"sv))
            return SniffedFormat::Media;
        return SniffedFormat::Unknown;
    }

    if (hasAt(head, 4, "ftyp"sv))
        return isoMediaFormat(head);

    if (hasAt(head, 0, "\x1A\x45\xDF\xA3"sv) || hasAt(head, 0, "OggS"sv) ||
        hasAt(head, 0, "fLaC"sv) || hasAt(head, 0, "FLV\x01"sv) ||
        hasAt(head, 0, "\0\0\x01\xBA"sv) || hasAt(head, 0, "\0\0\x01\xB3"sv) ||
        hasAt(head, 0, "\x30\x26\xB2\x75\x8E\x66\xCF\x11\xA6\xD9\x00\xAA\x00\x62\xCE\x6C"sv))
        return SniffedFormat::Media;
    // An ID3v2 tag, whose version byte is what keeps this from matching any text
    // that happens to start with those letters.
    if (hasAt(head, 0, "ID3"sv) && head.size() > 3 && head[3] >= 2 && head[3] <= 4)
        return SniffedFormat::Media;

    if (hasAt(head, 0, "PK\x03\x04"sv) || hasAt(head, 0, "PK\x05\x06"sv) ||
        hasAt(head, 0, "PK\x07\x08"sv))
        return SniffedFormat::Zip;

    // Readers accept junk ahead of the header, so it is searched for, not anchored.
    if (std::string_view(head.data(), head.size()).find("%PDF-"sv) != std::string_view::npos)
        return SniffedFormat::Pdf;

    return SniffedFormat::Unknown;
}

bool sniffRulesOut(SniffedFormat sniffed, PreviewKind chosen)
{
    // No viewer was named, so there is nothing for the bytes to contradict.
    if (chosen == PreviewKind::None || chosen == PreviewKind::Text)
        return false;
    switch (sniffed)
    {
        case SniffedFormat::Unknown:
            return false;
        case SniffedFormat::Image:
            return chosen != PreviewKind::Image;
        case SniffedFormat::Media:
            return chosen != PreviewKind::Video && chosen != PreviewKind::Audio;
        case SniffedFormat::Pdf:
            return chosen != PreviewKind::Pdf;
        case SniffedFormat::Zip:
            return chosen != PreviewKind::Archive;
    }
    return false;
}
