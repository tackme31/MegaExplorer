#include "platform/WindowsSessionStore.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>

namespace
{

// One unique temp file per test (test name embedded) so parallel/repeated
// runs never collide -- no shared fixture/SetUp-TearDown needed, mirrors
// SqliteNodeCacheTest.cpp's fixture-less style.
std::filesystem::path tempSessionPath(const std::string& testName)
{
    return std::filesystem::temp_directory_path() /
           ("megaexplorer_session_test_" + testName + ".bin");
}

void removeIfExists(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

} // namespace

TEST(WindowsSessionStoreTest, LoadOnNonexistentPathReturnsEmptyOk)
{
    // Arrange
    const auto path = tempSessionPath("nonexistent");
    removeIfExists(path);
    WindowsSessionStore store(path.string());

    // Act
    Result<std::string> result = store.loadSession();

    // Assert
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.value(), "");
}

TEST(WindowsSessionStoreTest, SaveThenLoadRoundTripsToken)
{
    // Arrange
    const auto path = tempSessionPath("roundtrip");
    removeIfExists(path);
    WindowsSessionStore store(path.string());
    const std::string token = "sample-session-token-value";

    // Act
    Result<void> saveResult = store.saveSession(token);
    Result<std::string> loadResult = store.loadSession();

    // Assert
    ASSERT_TRUE(saveResult.success);
    ASSERT_TRUE(loadResult.success);
    EXPECT_EQ(loadResult.value(), token);

    removeIfExists(path);
}

TEST(WindowsSessionStoreTest, SecondSaveOverwritesFirst)
{
    // Arrange
    const auto path = tempSessionPath("overwrite");
    removeIfExists(path);
    WindowsSessionStore store(path.string());

    // Act
    ASSERT_TRUE(store.saveSession("first-token").success);
    ASSERT_TRUE(store.saveSession("second-token").success);
    Result<std::string> loadResult = store.loadSession();

    // Assert
    ASSERT_TRUE(loadResult.success);
    EXPECT_EQ(loadResult.value(), "second-token");

    removeIfExists(path);
}

TEST(WindowsSessionStoreTest, ClearThenLoadReturnsEmptyOk)
{
    // Arrange
    const auto path = tempSessionPath("clear_after_save");
    removeIfExists(path);
    WindowsSessionStore store(path.string());
    ASSERT_TRUE(store.saveSession("some-token").success);

    // Act
    Result<void> clearResult = store.clearSession();
    Result<std::string> loadResult = store.loadSession();

    // Assert
    ASSERT_TRUE(clearResult.success);
    ASSERT_TRUE(loadResult.success);
    EXPECT_EQ(loadResult.value(), "");
}

TEST(WindowsSessionStoreTest, ClearOnNeverWrittenPathIsStillOk)
{
    // Arrange
    const auto path = tempSessionPath("clear_never_written");
    removeIfExists(path);
    WindowsSessionStore store(path.string());

    // Act
    Result<void> clearResult = store.clearSession();

    // Assert: idempotent, must succeed even though nothing was ever stored.
    EXPECT_TRUE(clearResult.success);
}

TEST(WindowsSessionStoreTest, GarbageBytesFileMakesLoadFailNotCrash)
{
    // Arrange: write raw non-DPAPI bytes directly, bypassing saveSession.
    const auto path = tempSessionPath("garbage");
    removeIfExists(path);
    {
        std::ofstream out(path, std::ios::binary);
        out << "this is not a valid DPAPI blob";
    }
    WindowsSessionStore store(path.string());

    // Act
    Result<std::string> loadResult = store.loadSession();

    // Assert
    EXPECT_FALSE(loadResult.success);

    removeIfExists(path);
}
