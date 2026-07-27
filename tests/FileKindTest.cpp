#include "core/FileKind.h"

#include <gtest/gtest.h>

TEST(FileKindTest, ExtractsAndUppercasesExtension)
{
    EXPECT_EQ(fileExtensionUppercased("photo.jpg"), "JPG");
    EXPECT_EQ(fileExtensionUppercased("document.PDF"), "PDF");
    EXPECT_EQ(fileExtensionUppercased("Report.Docx"), "DOCX");
}

TEST(FileKindTest, NoExtensionReturnsEmptyString)
{
    EXPECT_EQ(fileExtensionUppercased("README"), "");
}

TEST(FileKindTest, NameEndingInBareDotReturnsEmptyString)
{
    EXPECT_EQ(fileExtensionUppercased("archive."), "");
}

TEST(FileKindTest, LeadingDotNameIsTreatedAsExtensionAfterLastDot)
{
    EXPECT_EQ(fileExtensionUppercased(".gitignore"), "GITIGNORE");
}

TEST(FileKindTest, NameWithMultipleDotsUsesOnlyLastSegment)
{
    EXPECT_EQ(fileExtensionUppercased("archive.tar.gz"), "GZ");
}
