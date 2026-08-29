#include "core/PreviewKind.h"

#include <gtest/gtest.h>

TEST(PreviewKindTest, MediaExtensionsSplitIntoImageVideoAndPdf)
{
    EXPECT_EQ(previewKindForName("photo.jpg"), PreviewKind::Image);
    EXPECT_EQ(previewKindForName("raw.cr2"), PreviewKind::Image);
    EXPECT_EQ(previewKindForName("clip.mp4"), PreviewKind::Video);
    EXPECT_EQ(previewKindForName("manual.pdf"), PreviewKind::Pdf);
}

TEST(PreviewKindTest, TextAndSourceExtensionsAreText)
{
    EXPECT_EQ(previewKindForName("notes.txt"), PreviewKind::Text);
    EXPECT_EQ(previewKindForName("data.json"), PreviewKind::Text);
    EXPECT_EQ(previewKindForName("Main.qml"), PreviewKind::Text);
    // Classified as text rather than image on purpose -- see PreviewKind.cpp.
    EXPECT_EQ(previewKindForName("icon.svg"), PreviewKind::Text);
}

TEST(PreviewKindTest, SourceAndConfigExtensionsBeyondTheOriginalSetAreText)
{
    EXPECT_EQ(previewKindForName("App.kt"), PreviewKind::Text);
    EXPECT_EQ(previewKindForName("main.swift"), PreviewKind::Text);
    EXPECT_EQ(previewKindForName("analysis.ipynb"), PreviewKind::Text);
    EXPECT_EQ(previewKindForName("compose.yaml"), PreviewKind::Text);
    EXPECT_EQ(previewKindForName("build.gradle"), PreviewKind::Text);
    EXPECT_EQ(previewKindForName("schema.proto"), PreviewKind::Text);
    EXPECT_EQ(previewKindForName("movie.srt"), PreviewKind::Text);
    EXPECT_EQ(previewKindForName("fix.patch"), PreviewKind::Text);
}

TEST(PreviewKindTest, MediaWinsWhenAnExtensionIsClaimedByBothLists)
{
    // ".ts" is MPEG-TS as well as TypeScript, and the media list is consulted first:
    // a real transport stream has a server-side preview, TypeScript source has none.
    EXPECT_EQ(previewKindForName("stream.ts"), PreviewKind::Video);
    EXPECT_EQ(previewKindForName("component.tsx"), PreviewKind::Text);
}

TEST(PreviewKindTest, ComparisonIsCaseInsensitive)
{
    EXPECT_EQ(previewKindForName("NOTES.TXT"), PreviewKind::Text);
    EXPECT_EQ(previewKindForName("Photo.JPEG"), PreviewKind::Image);
}

TEST(PreviewKindTest, UnknownExtensionsAreNone)
{
    EXPECT_EQ(previewKindForName("setup.exe"), PreviewKind::None);
    EXPECT_EQ(previewKindForName("archive.zip"), PreviewKind::Archive);
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
    EXPECT_EQ(previewKindForName("backup.txt.zip"), PreviewKind::Archive);
    EXPECT_EQ(previewKindForName("archive.zip.txt"), PreviewKind::Text);
}
