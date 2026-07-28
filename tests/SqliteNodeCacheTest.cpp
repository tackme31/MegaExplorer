#include "platform/SqliteNodeCache.h"

#include <gtest/gtest.h>

namespace
{

FileEntry makeEntry(const std::string& name,
                    std::uint64_t handle,
                    std::uint64_t sizeBytes,
                    bool isFolder,
                    std::int64_t mtime = 0,
                    bool hasThumbnail = false)
{
    FileEntry entry;
    entry.name = name;
    entry.handle = handle;
    entry.sizeBytes = sizeBytes;
    entry.isFolder = isFolder;
    entry.modificationTime = mtime;
    entry.hasThumbnail = hasThumbnail;
    return entry;
}

void expectSameEntries(const std::vector<FileEntry>& actual, const std::vector<FileEntry>& expected)
{
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        EXPECT_EQ(actual[i].name, expected[i].name);
        EXPECT_EQ(actual[i].handle, expected[i].handle);
        EXPECT_EQ(actual[i].sizeBytes, expected[i].sizeBytes);
        EXPECT_EQ(actual[i].isFolder, expected[i].isFolder);
        EXPECT_EQ(actual[i].modificationTime, expected[i].modificationTime);
        EXPECT_EQ(actual[i].hasThumbnail, expected[i].hasThumbnail);
    }
}

} // namespace

TEST(SqliteNodeCacheTest, EmptyCacheReturnsSuccessWithEmptyResult)
{
    // Arrange
    SqliteNodeCache cache(":memory:");

    // Act
    Result<std::vector<FileEntry>> result = cache.loadChildren(INodeCache::ParentKey{false, 1});

    // Assert: a genuine SQL query with zero matching rows is not an error --
    // it's a successful, empty result. Distinguishing "never cached" from
    // "cached as empty" is explicitly out of scope for this store (see
    // INodeCache.h) -- FolderNavigationService is the layer that treats an
    // empty result as "nothing to show yet."
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.value.empty());
}

TEST(SqliteNodeCacheTest, SaveThenLoadRoundTripsEntries)
{
    // Arrange
    SqliteNodeCache cache(":memory:");
    const INodeCache::ParentKey key{false, 1};
    const std::vector<FileEntry> entries{
        makeEntry("a.txt", 10, 100, false, 12345, true),
        makeEntry("subfolder", 11, 0, true, 0, false),
    };

    // Act
    Result<void> saveResult = cache.saveChildren(key, entries);
    Result<std::vector<FileEntry>> loadResult = cache.loadChildren(key);

    // Assert
    ASSERT_TRUE(saveResult.success);
    ASSERT_TRUE(loadResult.success);
    expectSameEntries(loadResult.value, entries);
}

TEST(SqliteNodeCacheTest, ResaveReplacesPreviousEntriesRatherThanAppending)
{
    // Arrange
    SqliteNodeCache cache(":memory:");
    const INodeCache::ParentKey key{false, 1};
    const std::vector<FileEntry> firstSave{makeEntry("old.txt", 10, 100, false)};
    const std::vector<FileEntry> secondSave{makeEntry("new.txt", 20, 200, false)};

    // Act
    ASSERT_TRUE(cache.saveChildren(key, firstSave).success);
    ASSERT_TRUE(cache.saveChildren(key, secondSave).success);
    Result<std::vector<FileEntry>> loadResult = cache.loadChildren(key);

    // Assert: only secondSave's row survives -- a full replace, not an append.
    ASSERT_TRUE(loadResult.success);
    expectSameEntries(loadResult.value, secondSave);
}

TEST(SqliteNodeCacheTest, RootAndNonRootWithSameHandleAreDistinctKeys)
{
    // Arrange
    SqliteNodeCache cache(":memory:");
    const INodeCache::ParentKey rootKey{true, 0};
    const INodeCache::ParentKey nonRootKey{false, 0}; // same handle value (0), different isRoot
    const std::vector<FileEntry> rootEntries{makeEntry("root-child.txt", 1, 10, false)};
    const std::vector<FileEntry> nonRootEntries{makeEntry("folder0-child.txt", 2, 20, false)};

    // Act
    ASSERT_TRUE(cache.saveChildren(rootKey, rootEntries).success);
    ASSERT_TRUE(cache.saveChildren(nonRootKey, nonRootEntries).success);

    // Assert: no cross-contamination between the two keys despite the
    // colliding handle -- this is exactly what the composite primary key
    // (parent_is_root, parent_handle, handle) is meant to guarantee.
    expectSameEntries(cache.loadChildren(rootKey).value, rootEntries);
    expectSameEntries(cache.loadChildren(nonRootKey).value, nonRootEntries);
}

TEST(SqliteNodeCacheTest, DifferentParentHandlesAreIsolated)
{
    // Arrange
    SqliteNodeCache cache(":memory:");
    const INodeCache::ParentKey key1{false, 1};
    const INodeCache::ParentKey key2{false, 2};
    const std::vector<FileEntry> entries1{makeEntry("in-folder-1.txt", 10, 10, false)};

    // Act
    ASSERT_TRUE(cache.saveChildren(key1, entries1).success);
    Result<std::vector<FileEntry>> loadResult2 = cache.loadChildren(key2);

    // Assert
    expectSameEntries(cache.loadChildren(key1).value, entries1);
    ASSERT_TRUE(loadResult2.success);
    EXPECT_TRUE(loadResult2.value.empty());
}

TEST(SqliteNodeCacheTest, UnopenableDbPathDegradesGracefullyWithoutCrashing)
{
    // Arrange + Act: a path inside a directory that doesn't exist and can't
    // be auto-created by sqlite3_open (unlike ":memory:" or a plain
    // filename in an existing directory).
    SqliteNodeCache cache("Z:/this/path/does/not/exist/node_cache.sqlite3");

    // Assert: construction doesn't throw (we got here), and every
    // subsequent call fails cleanly rather than touching a null/invalid
    // sqlite3 handle.
    Result<std::vector<FileEntry>> loadResult = cache.loadChildren(INodeCache::ParentKey{true, 0});
    EXPECT_FALSE(loadResult.success);

    Result<void> saveResult =
        cache.saveChildren(INodeCache::ParentKey{true, 0}, std::vector<FileEntry>{});
    EXPECT_FALSE(saveResult.success);
}
