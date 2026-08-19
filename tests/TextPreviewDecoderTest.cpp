#include "qml/TextPreviewDecoder.h"

#include <gtest/gtest.h>

namespace
{
// Spelled as escapes rather than pasted characters: this file has no BOM, and MSVC
// would otherwise read it in the system codepage instead of UTF-8. QStringLiteral
// expands to a u"" literal, so the universal character names are unambiguous.
const QString kNihongo = QStringLiteral("\u65E5\u672C\u8A9E"); // "Japanese"
} // namespace

TEST(TextPreviewDecoderTest, PlainAsciiRoundTrips)
{
    const auto decoded = decodePreviewText(QByteArray("hello\nworld\n"));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, QStringLiteral("hello\nworld\n"));
}

TEST(TextPreviewDecoderTest, EmptyInputIsEmptyTextNotBinary)
{
    // An empty file is a perfectly good text file; nullopt would show "not text".
    const auto decoded = decodePreviewText(QByteArray());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_TRUE(decoded->isEmpty());
}

TEST(TextPreviewDecoderTest, Utf8BomIsStripped)
{
    QByteArray bytes("\xEF\xBB\xBF", 3);
    bytes.append("hello");

    const auto decoded = decodePreviewText(bytes);
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, QStringLiteral("hello"));
}

TEST(TextPreviewDecoderTest, Utf8JapaneseDecodes)
{
    const auto decoded = decodePreviewText(kNihongo.toUtf8());
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, kNihongo);
}

TEST(TextPreviewDecoderTest, Utf16SurvivesTheBinaryCheck)
{
    // Half the bytes of Latin UTF-16 are NUL, so the BOM has to be read before the
    // binary check rather than after it.
    const char utf16le[] = {'\xFF', '\xFE', 'h', 0, 'e', 0, 'l', 0, 'l', 0, 'o', 0};

    const auto decoded = decodePreviewText(QByteArray(utf16le, sizeof(utf16le)));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, QStringLiteral("hello"));
}

TEST(TextPreviewDecoderTest, EmbeddedNulMeansBinary)
{
    const char binary[] = {'M', 'Z', '\x90', 0, 'm', 'o', 'r', 'e'};

    EXPECT_FALSE(decodePreviewText(QByteArray(binary, sizeof(binary))).has_value());
}

TEST(TextPreviewDecoderTest, Cp932JapaneseFallsBackFromUtf8)
{
    // The same three characters in Shift_JIS, which is invalid as UTF-8 -- that
    // invalidity is what triggers the fallback.
    const char cp932[] = {'\x93', '\xFA', '\x96', '\x7B', '\x8C', '\xEA'};

    const auto decoded = decodePreviewText(QByteArray(cp932, sizeof(cp932)));
    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(*decoded, kNihongo);
}
