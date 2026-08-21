#include "platform/QtLocalFileSystem.h"

#include <QTemporaryDir>

#include <windows.h> // SetFileAttributesW, for the hidden-file case

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace
{

void writeFile(const std::filesystem::path& path, const std::string& contents)
{
    std::ofstream out(path, std::ios::binary);
    out << contents;
}

// Unwraps the "could not list" answer for the cases that are not about it.
std::vector<LocalEntry> mustList(const QtLocalFileSystem& fs, const QString& path)
{
    const std::optional<std::vector<LocalEntry>> entries = fs.listDirectory(path.toStdString());
    EXPECT_TRUE(entries.has_value());
    return entries.value_or(std::vector<LocalEntry>{});
}

std::vector<std::string> namesOf(const std::vector<LocalEntry>& entries)
{
    std::vector<std::string> names;
    for (const LocalEntry& entry : entries)
        names.push_back(entry.name);
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace

TEST(QtLocalFileSystemTest, ListsFilesAndDirectoriesWithoutDotEntries)
{
    // Arrange
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path root(dir.path().toStdWString());
    writeFile(root / "a.txt", "hello");
    std::filesystem::create_directory(root / "sub");

    QtLocalFileSystem fs;

    // Act
    const std::vector<LocalEntry> entries = mustList(fs, dir.path());

    // Assert
    EXPECT_THAT(namesOf(entries), ::testing::ElementsAre("a.txt", "sub"));
    for (const LocalEntry& entry : entries)
    {
        EXPECT_EQ(entry.isDirectory, entry.name == "sub");
        EXPECT_EQ(entry.sizeBytes, entry.isDirectory ? 0u : 5u);
        // Native separators, because the path goes on to the SDK.
        EXPECT_NE(entry.path.find("\\" + entry.name), std::string::npos);
    }
}

TEST(QtLocalFileSystemTest, ListsHiddenFiles)
{
    // The SDK's recursive upload sends hidden files, so a listing that matched
    // Explorer's view would under-count the collisions the user is asked about.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path root(dir.path().toStdWString());
    writeFile(root / "hidden.txt", "hello");
    ASSERT_NE(SetFileAttributesW((root / "hidden.txt").wstring().c_str(), FILE_ATTRIBUTE_HIDDEN), 0);

    QtLocalFileSystem fs;

    EXPECT_THAT(namesOf(mustList(fs, dir.path())), ::testing::ElementsAre("hidden.txt"));
}

TEST(QtLocalFileSystemTest, KeepsNonAsciiNamesAsUtf8)
{
    // The names go straight to the MEGA SDK, which takes UTF-8: swapping the
    // toUtf8-based conversions for toLocal8Bit would corrupt them silently.
    // Escaped rather than written literally so the test does not depend on how
    // the compiler reads this file's own encoding.
    const char* const kUtf8Name = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E.txt";
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString path = dir.path() + "/" + QString::fromUtf8(kUtf8Name);
    writeFile(std::filesystem::path(path.toStdWString()), "hello");

    QtLocalFileSystem fs;

    const std::vector<LocalEntry> entries = mustList(fs, dir.path());
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].name, kUtf8Name);

    const std::optional<LocalEntry> entry = fs.entryFor(entries[0].path);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->name, kUtf8Name);
}

TEST(QtLocalFileSystemTest, ListingAMissingOrNonDirectoryPathAnswersNoListing)
{
    // Not an empty listing: the upload skip plan drops a branch it cannot see, so
    // "there is nothing here" and "I could not look" have to stay apart.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path root(dir.path().toStdWString());
    writeFile(root / "a.txt", "hello");

    QtLocalFileSystem fs;

    EXPECT_FALSE(fs.listDirectory((root / "nope").string()).has_value());
    EXPECT_FALSE(fs.listDirectory((root / "a.txt").string()).has_value());
}

TEST(QtLocalFileSystemTest, AnEmptyDirectoryListsAsEmptyRatherThanUnreadable)
{
    // The other side of the same line, and the one the readability probe could
    // break: an empty directory is a listing, just an empty one.
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path root(dir.path().toStdWString());
    std::filesystem::create_directory(root / "empty");

    QtLocalFileSystem fs;

    const std::optional<std::vector<LocalEntry>> entries =
        fs.listDirectory((root / "empty").string());
    ASSERT_TRUE(entries.has_value());
    EXPECT_TRUE(entries->empty());
}

TEST(QtLocalFileSystemTest, EntryForDistinguishesFilesFoldersAndMissingPaths)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const std::filesystem::path root(dir.path().toStdWString());
    writeFile(root / "a.txt", "hello");
    std::filesystem::create_directory(root / "sub");

    QtLocalFileSystem fs;

    const std::optional<LocalEntry> aFile = fs.entryFor((root / "a.txt").string());
    ASSERT_TRUE(aFile.has_value());
    EXPECT_EQ(aFile->name, "a.txt");
    EXPECT_FALSE(aFile->isDirectory);
    EXPECT_EQ(aFile->sizeBytes, 5u);

    const std::optional<LocalEntry> aFolder = fs.entryFor((root / "sub").string());
    ASSERT_TRUE(aFolder.has_value());
    EXPECT_TRUE(aFolder->isDirectory);

    EXPECT_FALSE(fs.entryFor((root / "nope").string()).has_value());
}
