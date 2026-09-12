#include "core/OpenWithEntry.h"

#include <gtest/gtest.h>

// The pure half of "Open with": which files an entry is offered for, and how the
// menu ID carries an index the C++ action vocabulary has no room for.

TEST(OpenWithEntryTest, EmptyExtensionListMatchesEverything)
{
    EXPECT_TRUE(openWithExtensionMatches("", "photo.jpg"));
    EXPECT_TRUE(openWithExtensionMatches("", "no-extension"));
    EXPECT_TRUE(openWithExtensionMatches("   ", "photo.jpg"));
    // Separators with nothing between them are still an empty list, so a user who
    // clears the field down to its commas does not end up matching nothing.
    EXPECT_TRUE(openWithExtensionMatches(", ;,", "photo.jpg"));
}

TEST(OpenWithEntryTest, MatchesOnTheExtensionRegardlessOfSpelling)
{
    EXPECT_TRUE(openWithExtensionMatches("jpg", "photo.jpg"));
    EXPECT_TRUE(openWithExtensionMatches(".jpg", "photo.jpg"));
    EXPECT_TRUE(openWithExtensionMatches("*.jpg", "photo.jpg"));
    EXPECT_TRUE(openWithExtensionMatches("JPG", "photo.jpg"));
    EXPECT_TRUE(openWithExtensionMatches("jpg", "PHOTO.JPG"));
}

TEST(OpenWithEntryTest, AcceptsCommaSemicolonAndSpaceSeparatedLists)
{
    EXPECT_TRUE(openWithExtensionMatches("jpg, png, webp", "a.png"));
    EXPECT_TRUE(openWithExtensionMatches("jpg;png;webp", "a.webp"));
    EXPECT_TRUE(openWithExtensionMatches("jpg png webp", "a.jpg"));
    EXPECT_FALSE(openWithExtensionMatches("jpg, png, webp", "a.gif"));
}

TEST(OpenWithEntryTest, ComparesOnlyTheLastExtension)
{
    EXPECT_TRUE(openWithExtensionMatches("gz", "archive.tar.gz"));
    EXPECT_FALSE(openWithExtensionMatches("tar", "archive.tar.gz"));
    // A dot inside a directory-looking name is not an extension boundary the
    // matcher has to care about -- the input is a file name, never a path.
    EXPECT_TRUE(openWithExtensionMatches("txt", "notes.v2.txt"));
}

TEST(OpenWithEntryTest, AFileWithNoExtensionMatchesOnlyAnEmptyList)
{
    EXPECT_FALSE(openWithExtensionMatches("jpg", "README"));
    EXPECT_FALSE(openWithExtensionMatches("jpg", "trailing."));
    EXPECT_TRUE(openWithExtensionMatches("", "README"));
}

TEST(OpenWithEntryTest, CustomActionIdRoundTripsThroughItsIndex)
{
    EXPECT_EQ(openWithCustomActionId(0), "openWithCustom:0");
    EXPECT_EQ(openWithCustomActionId(7), "openWithCustom:7");
    EXPECT_EQ(openWithCustomIndex(openWithCustomActionId(0)), 0);
    EXPECT_EQ(openWithCustomIndex(openWithCustomActionId(12)), 12);
}

TEST(OpenWithEntryTest, CustomIndexRejectsAnythingButDigits)
{
    EXPECT_EQ(openWithCustomIndex("openWithCustom"), -1);
    EXPECT_EQ(openWithCustomIndex("openWithCustom:"), -1);
    EXPECT_EQ(openWithCustomIndex("openWithCustom:x"), -1);
    EXPECT_EQ(openWithCustomIndex("openWithCustom:1x"), -1);
    EXPECT_EQ(openWithCustomIndex("openWithCustom:-1"), -1);
    EXPECT_EQ(openWithCustomIndex("openWithBrowser"), -1);
    EXPECT_EQ(openWithCustomIndex(""), -1);
    // Far past any plausible list, so a hand-typed ID cannot make the parse
    // overflow rather than be rejected.
    EXPECT_EQ(openWithCustomIndex("openWithCustom:99999999999"), -1);
}
