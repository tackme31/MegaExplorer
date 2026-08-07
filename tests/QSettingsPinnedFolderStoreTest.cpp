#include "platform/QSettingsPinnedFolderStore.h"

#include "core/MegaErrorCodes.h"

#include <QSettings>
#include <QString>

#include <filesystem>
#include <fstream>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::ElementsAre;
using ::testing::IsEmpty;

namespace
{

// One unique temp INI per test (test name embedded), mirroring
// WindowsSessionStoreTest.cpp. Uniqueness matters more here than there:
// QSettings keeps a process-wide cache keyed by file path, so two tests
// sharing a path could see each other's in-memory state.
std::filesystem::path tempSettingsPath(const std::string& testName)
{
    return std::filesystem::temp_directory_path() / ("megaexplorer_pins_test_" + testName + ".ini");
}

void removeIfExists(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

PinnedFolder makePin(const char* name, std::uint64_t handle)
{
    PinnedFolder pin;
    pin.name = name;
    pin.handle = handle;
    return pin;
}

// Plants a stored value the store itself would never write, to reach load()'s
// rejection paths. The key layout is duplicated from the production file on
// purpose: the on-disk format is the contract under test.
void writeRawValue(const std::filesystem::path& path,
                   const std::string& accountKey,
                   const QString& text)
{
    QSettings settings(QString::fromStdString(path.string()), QSettings::IniFormat);
    settings.setValue(QStringLiteral("quickAccess/accounts/%1/pinnedFolders")
                          .arg(QString::fromStdString(accountKey)),
                      text);
    settings.sync();
}

const char* const kAccount = "1234567890";
const char* const kOtherAccount = "9876543210";

} // namespace

TEST(QSettingsPinnedFolderStoreTest, LoadOnUnwrittenStoreReturnsEmptyOk)
{
    // Arrange
    const auto path = tempSettingsPath("unwritten");
    removeIfExists(path);
    const QSettingsPinnedFolderStore store(path.string());

    // Act
    Result<std::vector<PinnedFolder>> result = store.load(kAccount);

    // Assert
    ASSERT_TRUE(result.success);
    EXPECT_THAT(result.value(), IsEmpty());

    removeIfExists(path);
}

TEST(QSettingsPinnedFolderStoreTest, SaveThenLoadRoundTripsPinsInOrder)
{
    // Arrange
    const auto path = tempSettingsPath("roundtrip");
    removeIfExists(path);
    QSettingsPinnedFolderStore store(path.string());

    // Act
    Result<void> saveResult =
        store.save(kAccount, {makePin("Photos", 11), makePin("Docs", 22), makePin("Music", 33)});
    Result<std::vector<PinnedFolder>> loadResult = store.load(kAccount);

    // Assert
    ASSERT_TRUE(saveResult.success);
    ASSERT_TRUE(loadResult.success);
    EXPECT_THAT(loadResult.value(),
                ElementsAre(makePin("Photos", 11), makePin("Docs", 22), makePin("Music", 33)));

    removeIfExists(path);
}

TEST(QSettingsPinnedFolderStoreTest, SaveThenLoadPreservesLargeHandleExactly)
{
    // Arrange: 48 bits set, the widest handle MEGA hands out -- the value the
    // production comment claims round-trips exactly through a JSON double.
    const auto path = tempSettingsPath("large_handle");
    removeIfExists(path);
    QSettingsPinnedFolderStore store(path.string());
    const std::uint64_t handle = 0xFFFFFFFFFFFFull;

    // Act
    ASSERT_TRUE(store.save(kAccount, {makePin("Wide", handle)}).success);
    Result<std::vector<PinnedFolder>> loadResult = store.load(kAccount);

    // Assert
    ASSERT_TRUE(loadResult.success);
    ASSERT_EQ(loadResult.value().size(), 1u);
    EXPECT_EQ(loadResult.value()[0].handle, handle);

    removeIfExists(path);
}

TEST(QSettingsPinnedFolderStoreTest, SaveThenLoadPreservesNonAsciiAndPunctuatedNames)
{
    // Arrange: a comma and a quote would break INI list/quoting rules if
    // QSettings' escaping were bypassed; the UTF-8 name checks the
    // std::string <-> QString conversions at both ends.
    const auto path = tempSettingsPath("tricky_names");
    removeIfExists(path);
    QSettingsPinnedFolderStore store(path.string());
    const char* const japanese = "\xE5\x86\x99\xE7\x9C\x9F"; // 写真
    const char* const punctuated = "a, b \"quoted\" \\ end";

    // Act
    ASSERT_TRUE(store.save(kAccount, {makePin(japanese, 7), makePin(punctuated, 8)}).success);
    Result<std::vector<PinnedFolder>> loadResult = store.load(kAccount);

    // Assert
    ASSERT_TRUE(loadResult.success);
    EXPECT_THAT(loadResult.value(), ElementsAre(makePin(japanese, 7), makePin(punctuated, 8)));

    removeIfExists(path);
}

TEST(QSettingsPinnedFolderStoreTest, SaveEmptyListThenLoadReturnsEmptyOk)
{
    // Arrange
    const auto path = tempSettingsPath("save_empty");
    removeIfExists(path);
    QSettingsPinnedFolderStore store(path.string());

    // Act
    Result<void> saveResult = store.save(kAccount, {});
    Result<std::vector<PinnedFolder>> loadResult = store.load(kAccount);

    // Assert
    ASSERT_TRUE(saveResult.success);
    ASSERT_TRUE(loadResult.success);
    EXPECT_THAT(loadResult.value(), IsEmpty());

    removeIfExists(path);
}

TEST(QSettingsPinnedFolderStoreTest, NewInstanceSeesWhatPreviousInstanceSaved)
{
    // Arrange: what an app restart does -- the pins outlive the store object.
    const auto path = tempSettingsPath("new_instance");
    removeIfExists(path);
    {
        QSettingsPinnedFolderStore writer(path.string());
        ASSERT_TRUE(writer.save(kAccount, {makePin("Photos", 11)}).success);
    }

    // Act
    const QSettingsPinnedFolderStore reader(path.string());
    Result<std::vector<PinnedFolder>> loadResult = reader.load(kAccount);

    // Assert
    ASSERT_TRUE(loadResult.success);
    EXPECT_THAT(loadResult.value(), ElementsAre(makePin("Photos", 11)));

    removeIfExists(path);
}

TEST(QSettingsPinnedFolderStoreTest, SavingShorterListDropsRemovedEntries)
{
    // Arrange: the case the single-JSON-string format exists for -- with
    // QSettings' array API the trailing indices would survive the shrink.
    const auto path = tempSettingsPath("shrink");
    removeIfExists(path);
    QSettingsPinnedFolderStore store(path.string());
    ASSERT_TRUE(store
                    .save(kAccount,
                          {makePin("a", 1),
                           makePin("b", 2),
                           makePin("c", 3),
                           makePin("d", 4),
                           makePin("e", 5)})
                    .success);

    // Act
    ASSERT_TRUE(store.save(kAccount, {makePin("a", 1), makePin("b", 2)}).success);
    Result<std::vector<PinnedFolder>> loadResult = store.load(kAccount);

    // Assert
    ASSERT_TRUE(loadResult.success);
    EXPECT_THAT(loadResult.value(), ElementsAre(makePin("a", 1), makePin("b", 2)));

    removeIfExists(path);
}

TEST(QSettingsPinnedFolderStoreTest, SavingEmptyListAfterNonEmptyClearsAll)
{
    // Arrange
    const auto path = tempSettingsPath("shrink_to_empty");
    removeIfExists(path);
    QSettingsPinnedFolderStore store(path.string());
    ASSERT_TRUE(store.save(kAccount, {makePin("a", 1), makePin("b", 2)}).success);

    // Act
    ASSERT_TRUE(store.save(kAccount, {}).success);
    Result<std::vector<PinnedFolder>> loadResult = store.load(kAccount);

    // Assert
    ASSERT_TRUE(loadResult.success);
    EXPECT_THAT(loadResult.value(), IsEmpty());

    removeIfExists(path);
}

TEST(QSettingsPinnedFolderStoreTest, PinsAreScopedPerAccountKey)
{
    // Arrange
    const auto path = tempSettingsPath("per_account");
    removeIfExists(path);
    QSettingsPinnedFolderStore store(path.string());

    // Act
    ASSERT_TRUE(store.save(kAccount, {makePin("mine", 1)}).success);
    ASSERT_TRUE(store.save(kOtherAccount, {makePin("theirs", 2)}).success);

    // Assert
    Result<std::vector<PinnedFolder>> first = store.load(kAccount);
    Result<std::vector<PinnedFolder>> second = store.load(kOtherAccount);
    ASSERT_TRUE(first.success);
    ASSERT_TRUE(second.success);
    EXPECT_THAT(first.value(), ElementsAre(makePin("mine", 1)));
    EXPECT_THAT(second.value(), ElementsAre(makePin("theirs", 2)));

    removeIfExists(path);
}

TEST(QSettingsPinnedFolderStoreTest, LoadForUnknownAccountReturnsEmptyOkWhileAnotherHasPins)
{
    // Arrange: the Phase 11a bug in its read half -- a freshly logged-in
    // account used to inherit the previous account's pins.
    const auto path = tempSettingsPath("unknown_account");
    removeIfExists(path);
    QSettingsPinnedFolderStore store(path.string());
    ASSERT_TRUE(store.save(kAccount, {makePin("mine", 1)}).success);

    // Act
    Result<std::vector<PinnedFolder>> loadResult = store.load(kOtherAccount);

    // Assert
    ASSERT_TRUE(loadResult.success);
    EXPECT_THAT(loadResult.value(), IsEmpty());

    removeIfExists(path);
}

TEST(QSettingsPinnedFolderStoreTest, SaveForOneAccountLeavesAnotherAccountUntouched)
{
    // Arrange: the Phase 11a bug in its write half -- saving used to overwrite
    // the other account's list.
    const auto path = tempSettingsPath("cross_account_write");
    removeIfExists(path);
    QSettingsPinnedFolderStore store(path.string());
    ASSERT_TRUE(store.save(kAccount, {makePin("mine", 1), makePin("mine2", 2)}).success);

    // Act
    ASSERT_TRUE(store.save(kOtherAccount, {}).success);

    // Assert
    Result<std::vector<PinnedFolder>> loadResult = store.load(kAccount);
    ASSERT_TRUE(loadResult.success);
    EXPECT_THAT(loadResult.value(), ElementsAre(makePin("mine", 1), makePin("mine2", 2)));

    removeIfExists(path);
}

TEST(QSettingsPinnedFolderStoreTest, CorruptStoredTextMakesLoadFail)
{
    // Arrange
    const auto path = tempSettingsPath("corrupt");
    removeIfExists(path);
    writeRawValue(path, kAccount, QStringLiteral("this is not JSON"));
    const QSettingsPinnedFolderStore store(path.string());

    // Act
    Result<std::vector<PinnedFolder>> loadResult = store.load(kAccount);

    // Assert
    EXPECT_FALSE(loadResult.success);
    EXPECT_EQ(loadResult.errorCode, MegaErrorCode::kEInternal);

    removeIfExists(path);
}

TEST(QSettingsPinnedFolderStoreTest, JsonObjectInsteadOfArrayMakesLoadFail)
{
    // Arrange: parses fine, but the top level must be the pin array.
    const auto path = tempSettingsPath("not_an_array");
    removeIfExists(path);
    writeRawValue(path, kAccount, QStringLiteral("{\"name\":\"Photos\",\"handle\":11}"));
    const QSettingsPinnedFolderStore store(path.string());

    // Act
    Result<std::vector<PinnedFolder>> loadResult = store.load(kAccount);

    // Assert
    EXPECT_FALSE(loadResult.success);
    EXPECT_EQ(loadResult.errorCode, MegaErrorCode::kEInternal);

    removeIfExists(path);
}

TEST(QSettingsPinnedFolderStoreTest, NonObjectArrayElementsAreSkipped)
{
    // Arrange
    const auto path = tempSettingsPath("non_object_elements");
    removeIfExists(path);
    writeRawValue(
        path, kAccount, QStringLiteral("[1,\"x\",null,{\"name\":\"Photos\",\"handle\":11}]"));
    const QSettingsPinnedFolderStore store(path.string());

    // Act
    Result<std::vector<PinnedFolder>> loadResult = store.load(kAccount);

    // Assert: a hand-edited entry costs its own pin, not the whole list.
    ASSERT_TRUE(loadResult.success);
    EXPECT_THAT(loadResult.value(), ElementsAre(makePin("Photos", 11)));

    removeIfExists(path);
}

TEST(QSettingsPinnedFolderStoreTest, ZeroAndMissingHandleEntriesAreSkipped)
{
    // Arrange: 0 is never a real node handle, so both spellings are junk.
    const auto path = tempSettingsPath("zero_handle");
    removeIfExists(path);
    writeRawValue(path,
                  kAccount,
                  QStringLiteral("[{\"name\":\"Zero\",\"handle\":0},{\"name\":\"NoHandle\"},"
                                 "{\"name\":\"Photos\",\"handle\":11}]"));
    const QSettingsPinnedFolderStore store(path.string());

    // Act
    Result<std::vector<PinnedFolder>> loadResult = store.load(kAccount);

    // Assert
    ASSERT_TRUE(loadResult.success);
    EXPECT_THAT(loadResult.value(), ElementsAre(makePin("Photos", 11)));

    removeIfExists(path);
}

TEST(QSettingsPinnedFolderStoreTest, SaveIntoUnwritablePathFails)
{
    // Arrange: an INI whose parent "directory" is an existing regular file, so
    // QSettings can neither create the directory nor the file.
    const auto blocker = tempSettingsPath("unwritable_blocker");
    removeIfExists(blocker);
    {
        std::ofstream out(blocker);
        out << "not a directory";
    }
    const auto path = blocker / "pins.ini";
    QSettingsPinnedFolderStore store(path.string());

    // Act
    Result<void> saveResult = store.save(kAccount, {makePin("Photos", 11)});

    // Assert
    EXPECT_FALSE(saveResult.success);
    EXPECT_EQ(saveResult.errorCode, MegaErrorCode::kEInternal);

    removeIfExists(blocker);
}

TEST(QSettingsPinnedFolderStoreTest, EntryMissingNameKeepsItsHandle)
{
    // Arrange: the handle is the identity, the name only a cached label, so a
    // nameless entry is still a usable pin.
    const auto path = tempSettingsPath("missing_name");
    removeIfExists(path);
    writeRawValue(path, kAccount, QStringLiteral("[{\"handle\":11}]"));
    const QSettingsPinnedFolderStore store(path.string());

    // Act
    Result<std::vector<PinnedFolder>> loadResult = store.load(kAccount);

    // Assert
    ASSERT_TRUE(loadResult.success);
    EXPECT_THAT(loadResult.value(), ElementsAre(makePin("", 11)));

    removeIfExists(path);
}
