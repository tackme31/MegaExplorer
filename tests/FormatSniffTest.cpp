#include "core/FormatSniff.h"

#include <algorithm>
#include <gtest/gtest.h>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

namespace
{

// Pads to a realistic read so a check that overruns its own signature would still
// find bytes to compare against. string_view, not std::string: most signatures hold
// a NUL, where a const char* would be cut short.
std::vector<char> head(std::string_view prefix, std::size_t padTo = 64)
{
    std::vector<char> bytes(prefix.begin(), prefix.end());
    if (bytes.size() < padTo)
        bytes.resize(padTo, '\x55');
    return bytes;
}

// An ftyp box: major brand, a zero minor version, then the compatible brands.
std::vector<char> isoFile(std::string_view major,
                          std::initializer_list<std::string_view> compatible = {})
{
    std::string bytes(4, '\0');
    bytes[3] = static_cast<char>(16 + 4 * compatible.size());
    bytes += "ftyp"sv;
    bytes += major;
    bytes.append(4, '\0');
    for (std::string_view brand : compatible)
        bytes += brand;
    return head(bytes);
}

std::vector<char> bitmap(char dibHeaderSize)
{
    std::string bytes("BM"sv);
    bytes.append(12, '\0');
    bytes += std::string{dibHeaderSize, '\0', '\0', '\0'};
    return head(bytes);
}

} // namespace

TEST(FormatSniffTest, ImageSignaturesAreImages)
{
    EXPECT_EQ(sniffFormat(head("\x89PNG\r\n\x1a\n"sv)), SniffedFormat::Image);
    EXPECT_EQ(sniffFormat(head("\xFF\xD8\xFF\xE0"sv)), SniffedFormat::Image);
    EXPECT_EQ(sniffFormat(head("GIF89a"sv)), SniffedFormat::Image);
    EXPECT_EQ(sniffFormat(head("GIF87a"sv)), SniffedFormat::Image);
    EXPECT_EQ(sniffFormat(head("RIFF\x10\0\0\0WEBP"sv)), SniffedFormat::Image);
    EXPECT_EQ(sniffFormat(head("II*\0"sv)), SniffedFormat::Image);
    EXPECT_EQ(sniffFormat(head("MM\0*"sv)), SniffedFormat::Image);
    EXPECT_EQ(sniffFormat(bitmap(40)), SniffedFormat::Image);
    EXPECT_EQ(sniffFormat(bitmap(124)), SniffedFormat::Image);
}

// Sharing MP4's box layout must not make a still look like a movie.
TEST(FormatSniffTest, IsoStillsAreImagesAndTheRestIsMedia)
{
    EXPECT_EQ(sniffFormat(isoFile("heic"sv)), SniffedFormat::Image);
    EXPECT_EQ(sniffFormat(isoFile("avif"sv)), SniffedFormat::Image);
    EXPECT_EQ(sniffFormat(isoFile("crx "sv)), SniffedFormat::Image);
    EXPECT_EQ(sniffFormat(isoFile("isom"sv)), SniffedFormat::Media);
    EXPECT_EQ(sniffFormat(isoFile("qt  "sv)), SniffedFormat::Media);
    EXPECT_EQ(sniffFormat(isoFile("M4A "sv)), SniffedFormat::Media);
    EXPECT_EQ(sniffFormat(isoFile("isom"sv, {"iso2"sv, "avc1"sv, "mp41"sv})), SniffedFormat::Media);
}

// A HEIF's major brand need not be one of its own; the compatible list decides then.
TEST(FormatSniffTest, AStillBrandAnywhereInTheBoxMakesAnImage)
{
    EXPECT_EQ(sniffFormat(isoFile("jpeg"sv, {"mif1"sv, "heic"sv})), SniffedFormat::Image);
    EXPECT_EQ(sniffFormat(isoFile("mif2"sv)), SniffedFormat::Image);
}

// Animated HEIF/AVIF can be played as video too, so it refuses neither viewer.
TEST(FormatSniffTest, ASequenceBrandProvesNothing)
{
    EXPECT_EQ(sniffFormat(isoFile("avis"sv, {"avif"sv, "msf1"sv})), SniffedFormat::Unknown);
    EXPECT_EQ(sniffFormat(isoFile("mif1"sv, {"heic"sv, "hevc"sv})), SniffedFormat::Unknown);
}

// Brands past the box's own end belong to the next box and are not read.
TEST(FormatSniffTest, BrandsPastTheFtypBoxAreIgnored)
{
    std::vector<char> bytes = isoFile("isom"sv);
    const std::string_view heic = "heic"sv;
    std::copy(heic.begin(), heic.end(), bytes.begin() + 16);
    bytes[3] = 16;
    EXPECT_EQ(sniffFormat(bytes), SniffedFormat::Media);
}

TEST(FormatSniffTest, AudioAndVideoContainersAreMedia)
{
    EXPECT_EQ(sniffFormat(head("\x1A\x45\xDF\xA3"sv)), SniffedFormat::Media);
    EXPECT_EQ(sniffFormat(head("RIFF\x10\0\0\0AVI "sv)), SniffedFormat::Media);
    EXPECT_EQ(sniffFormat(head("RIFF\x10\0\0\0WAVE"sv)), SniffedFormat::Media);
    EXPECT_EQ(sniffFormat(head("OggS"sv)), SniffedFormat::Media);
    EXPECT_EQ(sniffFormat(head("fLaC"sv)), SniffedFormat::Media);
    EXPECT_EQ(sniffFormat(head("FLV\x01"sv)), SniffedFormat::Media);
    EXPECT_EQ(sniffFormat(head("\0\0\x01\xBA"sv)), SniffedFormat::Media);
    EXPECT_EQ(
        sniffFormat(head("\x30\x26\xB2\x75\x8E\x66\xCF\x11\xA6\xD9\x00\xAA\x00\x62\xCE\x6C"sv)),
        SniffedFormat::Media);
    EXPECT_EQ(sniffFormat(head("ID3\x04\0"sv)), SniffedFormat::Media);
}

TEST(FormatSniffTest, ZipAndPdfAreTheirOwnFamilies)
{
    EXPECT_EQ(sniffFormat(head("PK\x03\x04"sv)), SniffedFormat::Zip);
    EXPECT_EQ(sniffFormat(head("PK\x05\x06"sv)), SniffedFormat::Zip);
    EXPECT_EQ(sniffFormat(head("%PDF-1.7\n"sv)), SniffedFormat::Pdf);
    // Readers accept a prefix before the header, so it need not be at offset 0.
    EXPECT_EQ(sniffFormat(head("junk\r\n%PDF-1.4"sv)), SniffedFormat::Pdf);
}

// Anything short of proof stays Unknown, which never refuses: a wrong refusal would
// block a file the viewer could have shown.
TEST(FormatSniffTest, WeakOrMissingSignaturesAreUnknown)
{
    EXPECT_EQ(sniffFormat({}), SniffedFormat::Unknown);
    EXPECT_EQ(sniffFormat(head("plain text, nothing more"sv)), SniffedFormat::Unknown);
    // Two letters any text could start with, without a DIB header size behind them.
    EXPECT_EQ(sniffFormat(head("BMW service record"sv)), SniffedFormat::Unknown);
    EXPECT_EQ(sniffFormat(bitmap(41)), SniffedFormat::Unknown);
    // An ID3 lookalike whose version byte is not one.
    EXPECT_EQ(sniffFormat(head("ID3 tags explained"sv)), SniffedFormat::Unknown);
    // A RIFF of some other kind says nothing about which viewer.
    EXPECT_EQ(sniffFormat(head("RIFF\x10\0\0\0CDXA"sv)), SniffedFormat::Unknown);
    // MPEG-TS sync byte and a raw MP3 frame header: real, but too weak to refuse on.
    EXPECT_EQ(sniffFormat(head("\x47\x40\x00\x10"sv)), SniffedFormat::Unknown);
    EXPECT_EQ(sniffFormat(head("\xFF\xFB\x90\x64"sv)), SniffedFormat::Unknown);
    // Shorter than every signature it starts like.
    EXPECT_EQ(sniffFormat(std::vector<char>{'\x89', 'P'}), SniffedFormat::Unknown);
    EXPECT_EQ(sniffFormat(std::vector<char>{'B', 'M'}), SniffedFormat::Unknown);
}

TEST(FormatSniffTest, OnlyAProvenOtherFamilyRulesAViewerOut)
{
    // With no viewer named there is nothing to contradict.
    EXPECT_FALSE(sniffRulesOut(SniffedFormat::Zip, PreviewKind::None));

    for (PreviewKind chosen : {PreviewKind::Image,
                               PreviewKind::Video,
                               PreviewKind::Audio,
                               PreviewKind::Pdf,
                               PreviewKind::Archive})
        EXPECT_FALSE(sniffRulesOut(SniffedFormat::Unknown, chosen)) << static_cast<int>(chosen);

    EXPECT_FALSE(sniffRulesOut(SniffedFormat::Image, PreviewKind::Image));
    EXPECT_TRUE(sniffRulesOut(SniffedFormat::Image, PreviewKind::Video));
    EXPECT_TRUE(sniffRulesOut(SniffedFormat::Image, PreviewKind::Pdf));

    // Which tracks a container holds is FFmpeg's call, not this one's.
    EXPECT_FALSE(sniffRulesOut(SniffedFormat::Media, PreviewKind::Video));
    EXPECT_FALSE(sniffRulesOut(SniffedFormat::Media, PreviewKind::Audio));
    EXPECT_TRUE(sniffRulesOut(SniffedFormat::Media, PreviewKind::Pdf));
    EXPECT_TRUE(sniffRulesOut(SniffedFormat::Media, PreviewKind::Image));

    EXPECT_FALSE(sniffRulesOut(SniffedFormat::Pdf, PreviewKind::Pdf));
    EXPECT_TRUE(sniffRulesOut(SniffedFormat::Pdf, PreviewKind::Archive));

    EXPECT_FALSE(sniffRulesOut(SniffedFormat::Zip, PreviewKind::Archive));
    EXPECT_TRUE(sniffRulesOut(SniffedFormat::Zip, PreviewKind::Audio));
}
