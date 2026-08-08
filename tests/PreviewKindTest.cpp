#include "core/PreviewKind.h"

#include <gtest/gtest.h>

TEST(PreviewKindTest, MediaExtensionsAreImageIncludingVideoAndPdf)
{
    EXPECT_EQ(previewKindForName("photo.jpg"), PreviewKind::Image);
    EXPECT_EQ(previewKindForName("raw.cr2"), PreviewKind::Image);
    EXPECT_EQ(previewKindForName("clip.mp4"), PreviewKind::Image);
    EXPECT_EQ(previewKindForName("manual.pdf"), PreviewKind::Image);
}

TEST(PreviewKindTest, TextAndSourceExtensionsAreText)
{
    EXPECT_EQ(previewKindForName("notes.txt"), PreviewKind::Text);
    EXPECT_EQ(previewKindForName("data.json"), PreviewKind::Text);
    EXPECT_EQ(previewKindForName("Main.qml"), PreviewKind::Text);
    // Classified as text rather than image on purpose -- see PreviewKind.cpp.
    EXPECT_EQ(previewKindForName("icon.svg"), PreviewKind::Text);
}

TEST(PreviewKindTest, ComparisonIsCaseInsensitive)
{
    EXPECT_EQ(previewKindForName("NOTES.TXT"), PreviewKind::Text);
    EXPECT_EQ(previewKindForName("Photo.JPEG"), PreviewKind::Image);
}

TEST(PreviewKindTest, UnknownExtensionsAreNone)
{
    EXPECT_EQ(previewKindForName("setup.exe"), PreviewKind::None);
    EXPECT_EQ(previewKindForName("archive.zip"), PreviewKind::None);
    // Audio has no server-side preview: USE_MEDIAINFO is off, so not even cover art.
    EXPECT_EQ(previewKindForName("song.mp3"), PreviewKind::None);
}

TEST(PreviewKindTest, NamesWithoutAUsableExtensionAreNone)
{
    EXPECT_EQ(previewKindForName("README"), PreviewKind::None);
    EXPECT_EQ(previewKindForName(""), PreviewKind::None);
    // A leading dot is a dotfile, not an extension.
    EXPECT_EQ(previewKindForName(".txt"), PreviewKind::None);
    EXPECT_EQ(previewKindForName("notes."), PreviewKind::None);
}

TEST(PreviewKindTest, OnlyTheLastExtensionCounts)
{
    EXPECT_EQ(previewKindForName("backup.txt.zip"), PreviewKind::None);
    EXPECT_EQ(previewKindForName("archive.zip.txt"), PreviewKind::Text);
}
