#include "core/UploadScanService.h"

#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"

#include <algorithm>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace
{

// An in-memory local tree. Records every directory it was asked to list, which is
// how the tests assert the scan does *not* descend into a branch.
class FakeLocalFileSystem : public ILocalFileSystem
{
public:
    void addFile(const std::string& parent, const std::string& name)
    {
        add(parent, name, false);
    }

    void addDirectory(const std::string& parent, const std::string& name)
    {
        const LocalEntry entry = add(parent, name, true);
        mChildren[entry.path]; // an empty directory still exists
    }

    // Resolves a forward-slash spelling too, the way the real adapter turns a
    // dropped URL into a native path -- so a test can hand the same file in under
    // two names, as a QML drop can.
    std::optional<LocalEntry> entryFor(const std::string& path) const override
    {
        std::string native = path;
        for (char& c : native)
        {
            if (c == '/')
                c = '\\';
        }
        const auto it = mEntries.find(native);
        if (it == mEntries.end())
            return std::nullopt;
        return it->second;
    }

    std::optional<std::vector<LocalEntry>> listDirectory(const std::string& path) const override
    {
        const bool relisting = std::find(listed.begin(), listed.end(), path) != listed.end();
        listed.push_back(path);
        if (unreadable.find(path) != unreadable.end() ||
            (relisting && unreadableOnRelisting.find(path) != unreadableOnRelisting.end()))
            return std::nullopt;
        const auto it = mChildren.find(path);
        if (it == mChildren.end())
            return std::nullopt;
        return it->second;
    }

    mutable std::vector<std::string> listed;
    // Paths that answer "could not read" instead of a listing. `unreadable` refuses
    // from the first call; `unreadableOnRelisting` only from the second, which is
    // the race the skip plan hits -- it re-lists what the scan already walked.
    std::set<std::string> unreadable;
    std::set<std::string> unreadableOnRelisting;

private:
    LocalEntry add(const std::string& parent, const std::string& name, bool isDirectory)
    {
        LocalEntry entry;
        entry.path = parent + "\\" + name;
        entry.name = name;
        entry.isDirectory = isDirectory;
        entry.sizeBytes = isDirectory ? 0u : 1u;
        mEntries[entry.path] = entry;
        mChildren[parent].push_back(entry);
        return entry;
    }

    std::map<std::string, LocalEntry> mEntries;
    std::map<std::string, std::vector<LocalEntry>> mChildren;
};

FileEntry file(const std::string& name, std::uint64_t handle)
{
    FileEntry entry;
    entry.name = name;
    entry.handle = handle;
    return entry;
}

FileEntry folder(const std::string& name, std::uint64_t handle)
{
    FileEntry entry = file(name, handle);
    entry.isFolder = true;
    return entry;
}

Result<std::vector<FileEntry>> hits(std::vector<FileEntry> entries)
{
    return Result<std::vector<FileEntry>>::ok(std::move(entries));
}

} // namespace

TEST(UploadScanServiceTest, ReportsATopLevelFileCollisionWithItsDestination)
{
    // Arrange
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addFile("C:\\src", "a.txt");
    fs->addFile("C:\\src", "b.txt");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFiles(7, false, std::vector<std::string>{"a.txt", "b.txt"}))
        .WillOnce(::testing::Return(hits({file("a.txt", 55)})));

    UploadScanService service(client, fs);

    // Act
    Result<std::vector<UploadCollision>> result =
        service.findCollisions({"C:\\src\\a.txt", "C:\\src\\b.txt"}, 7, false);

    // Assert
    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.value().size(), 1u);
    EXPECT_EQ(result.value()[0].localPath, "C:\\src\\a.txt");
    EXPECT_EQ(result.value()[0].name, "a.txt");
    EXPECT_EQ(result.value()[0].parentHandle, 7u);
    EXPECT_FALSE(result.value()[0].parentIsRoot);
    EXPECT_EQ(result.value()[0].existingHandle, 55u);
}

TEST(UploadScanServiceTest, SkipsLocalPathsThatNoLongerExist)
{
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addFile("C:\\src", "a.txt");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFiles(7, false, std::vector<std::string>{"a.txt"}))
        .WillOnce(::testing::Return(hits({})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadCollision>> result =
        service.findCollisions({"C:\\src\\a.txt", "C:\\src\\gone.txt"}, 7, false);

    ASSERT_TRUE(result.success);
    EXPECT_TRUE(result.value().empty());
}

TEST(UploadScanServiceTest, DoesNotDescendIntoAFolderMegaDoesNotHave)
{
    // The whole point of the level-at-a-time port: with no same-named folder on
    // MEGA nothing inside can collide, so the local tree is never walked.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addDirectory("C:\\src", "photos");
    fs->addFile("C:\\src\\photos", "a.jpg");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFiles(::testing::_, ::testing::_, ::testing::_)).Times(0);
    EXPECT_CALL(*client, findChildFolders(7, false, std::vector<std::string>{"photos"}))
        .WillOnce(::testing::Return(hits({})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadCollision>> result =
        service.findCollisions({"C:\\src\\photos"}, 7, false);

    ASSERT_TRUE(result.success);
    EXPECT_TRUE(result.value().empty());
    EXPECT_TRUE(fs->listed.empty());
}

TEST(UploadScanServiceTest, FindsACollisionNestedInsideAMergingFolder)
{
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addDirectory("C:\\src", "photos");
    fs->addFile("C:\\src\\photos", "a.jpg");
    fs->addFile("C:\\src\\photos", "new.jpg");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFolders(7, false, std::vector<std::string>{"photos"}))
        .WillOnce(::testing::Return(hits({folder("photos", 20)})));
    EXPECT_CALL(*client, findChildFiles(20, false, std::vector<std::string>{"a.jpg", "new.jpg"}))
        .WillOnce(::testing::Return(hits({file("a.jpg", 99)})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadCollision>> result =
        service.findCollisions({"C:\\src\\photos"}, 7, false);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.value().size(), 1u);
    EXPECT_EQ(result.value()[0].localPath, "C:\\src\\photos\\a.jpg");
    EXPECT_EQ(result.value()[0].parentHandle, 20u);
    EXPECT_EQ(result.value()[0].existingHandle, 99u);
    EXPECT_THAT(fs->listed, ::testing::ElementsAre("C:\\src\\photos"));
}

TEST(UploadScanServiceTest, AddingOneFileToAMergingFolderCollidesWithNothing)
{
    // SPEC_NAME_CONFLICT_UPLOAD.md 3-1: a same-named folder is not itself a
    // question, so this upload must reach the queue without a dialog.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addDirectory("C:\\src", "dir");
    fs->addFile("C:\\src\\dir", "file1.txt");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFolders(7, false, std::vector<std::string>{"dir"}))
        .WillOnce(::testing::Return(hits({folder("dir", 20)})));
    EXPECT_CALL(*client, findChildFiles(20, false, std::vector<std::string>{"file1.txt"}))
        .WillOnce(::testing::Return(hits({})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadCollision>> result =
        service.findCollisions({"C:\\src\\dir"}, 7, false);

    ASSERT_TRUE(result.success);
    EXPECT_TRUE(result.value().empty());
}

TEST(UploadScanServiceTest, FailsWhenAMegaLookupFails)
{
    // A caller has to be able to tell "nothing collides" from "could not ask".
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addFile("C:\\src", "a.txt");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFiles(7, false, std::vector<std::string>{"a.txt"}))
        .WillOnce(::testing::Return(
            Result<std::vector<FileEntry>>::fail("gone", MegaErrorCode::kENoEnt)));

    UploadScanService service(client, fs);

    Result<std::vector<UploadCollision>> result =
        service.findCollisions({"C:\\src\\a.txt"}, 7, false);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, MegaErrorCode::kENoEnt);
}

TEST(UploadScanServiceTest, StopsDescendingAtTheDepthLimit)
{
    // A directory junction pointing at its own ancestor makes the local tree
    // cyclic, and both sides answer "same name here" forever.
    class CyclicFileSystem : public ILocalFileSystem
    {
    public:
        std::optional<LocalEntry> entryFor(const std::string& path) const override
        {
            LocalEntry entry;
            entry.path = path;
            entry.name = "loop";
            entry.isDirectory = true;
            return entry;
        }

        std::optional<std::vector<LocalEntry>> listDirectory(const std::string& path) const override
        {
            ++levels;
            return std::vector<LocalEntry>{*entryFor(path + "\\loop")};
        }

        mutable int levels = 0;
    };

    auto fs = std::make_shared<CyclicFileSystem>();
    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFolders(::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(::testing::Return(hits({folder("loop", 20)})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadCollision>> result = service.findCollisions({"C:\\loop"}, 7, false);

    // Failure, not an empty answer: a truncated walk has not established that
    // nothing collides.
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, MegaErrorCode::kEInternal);
    EXPECT_EQ(fs->levels, UploadScanService::kMaxDepth - 1);
}

TEST(UploadScanServiceTest, CarriesTheRootFlagAtTheTopLevelOnlyAndAHandleBelowIt)
{
    // parentIsRoot true makes the handle beside it meaningless (IMegaClient.h),
    // so the flag must not survive into a nested level.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addFile("C:\\src", "top.txt");
    fs->addDirectory("C:\\src", "photos");
    fs->addFile("C:\\src\\photos", "nested.txt");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFiles(0, true, std::vector<std::string>{"top.txt"}))
        .WillOnce(::testing::Return(hits({file("top.txt", 11)})));
    EXPECT_CALL(*client, findChildFolders(0, true, std::vector<std::string>{"photos"}))
        .WillOnce(::testing::Return(hits({folder("photos", 20)})));
    EXPECT_CALL(*client, findChildFiles(20, false, std::vector<std::string>{"nested.txt"}))
        .WillOnce(::testing::Return(hits({file("nested.txt", 12)})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadCollision>> result =
        service.findCollisions({"C:\\src\\top.txt", "C:\\src\\photos"}, 0, true);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.value().size(), 2u);
    EXPECT_TRUE(result.value()[0].parentIsRoot);
    EXPECT_EQ(result.value()[0].parentHandle, 0u);
    EXPECT_FALSE(result.value()[1].parentIsRoot);
    EXPECT_EQ(result.value()[1].parentHandle, 20u);
}

TEST(UploadScanServiceTest, AsksAboutEachNameOnceButAnswersForEveryPathCarryingIt)
{
    // MEGA allows same-named siblings, so one local file must not become two rows.
    // One upload can also carry the same leaf name twice (dragged from different
    // folders), and both of those *do* get a row: the skip plan keys on path, so a
    // twin left out would upload over the node the other one spared.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addFile("C:\\a", "x.txt");
    fs->addFile("C:\\b", "x.txt");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFiles(7, false, std::vector<std::string>{"x.txt"}))
        .WillOnce(::testing::Return(hits({file("x.txt", 31), file("x.txt", 32)})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadCollision>> result =
        service.findCollisions({"C:\\a\\x.txt", "C:\\b\\x.txt"}, 7, false);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.value().size(), 2u);
    EXPECT_EQ(result.value()[0].localPath, "C:\\a\\x.txt");
    EXPECT_EQ(result.value()[1].localPath, "C:\\b\\x.txt");
    // First hit per name wins, for both of them.
    EXPECT_EQ(result.value()[0].existingHandle, 31u);
    EXPECT_EQ(result.value()[1].existingHandle, 31u);
}

TEST(UploadScanServiceTest, ReportsTheSecondCopyOfANameTheUploadBringsTwice)
{
    // Nothing on MEGA to land on, yet the two land on each other: whichever arrives
    // second becomes a version of the first (spec 1-3), so the question is the same
    // one the destination's own file would raise.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addFile("C:\\a", "x.txt");
    fs->addFile("C:\\b", "x.txt");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFiles(7, false, std::vector<std::string>{"x.txt"}))
        .WillOnce(::testing::Return(hits({})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadCollision>> result =
        service.findCollisions({"C:\\a\\x.txt", "C:\\b\\x.txt"}, 7, false);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.value().size(), 1u);
    EXPECT_EQ(result.value()[0].localPath, "C:\\b\\x.txt");
    EXPECT_EQ(result.value()[0].existingHandle, 0u);
}

TEST(UploadScanServiceTest, ReportsACollisionBetweenTwoCopiesOfAFolderMegaDoesNotHave)
{
    // The folder half of "the upload brings the name twice": the SDK creates
    // `photos` from the first copy and merges the second into it, so files the two
    // share stack as versions even though MEGA held neither.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addDirectory("C:\\a", "photos");
    fs->addFile("C:\\a\\photos", "p.jpg");
    fs->addDirectory("C:\\b", "photos");
    fs->addFile("C:\\b\\photos", "p.jpg");
    fs->addFile("C:\\b\\photos", "q.jpg");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFolders(7, false, std::vector<std::string>{"photos"}))
        .WillOnce(::testing::Return(hits({})));
    // Nothing to ask about below a folder that is not there yet.
    EXPECT_CALL(*client, findChildFiles(::testing::_, ::testing::_, ::testing::_)).Times(0);

    UploadScanService service(client, fs);

    Result<std::vector<UploadCollision>> result =
        service.findCollisions({"C:\\a\\photos", "C:\\b\\photos"}, 7, false);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.value().size(), 1u);
    EXPECT_EQ(result.value()[0].localPath, "C:\\b\\photos\\p.jpg");
    EXPECT_EQ(result.value()[0].existingHandle, 0u);
    EXPECT_EQ(result.value()[0].parentHandle, 0u);
    EXPECT_FALSE(result.value()[0].parentIsRoot);
}

TEST(UploadScanServiceTest, ReportsACollisionBetweenTwoCopiesMergingIntoOneMegaFolder)
{
    // Same defect one level in: MEGA has the folder but not the file, so each copy
    // on its own collides with nothing -- they only collide with each other.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addDirectory("C:\\a", "photos");
    fs->addFile("C:\\a\\photos", "p.jpg");
    fs->addDirectory("C:\\b", "photos");
    fs->addFile("C:\\b\\photos", "p.jpg");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFolders(7, false, std::vector<std::string>{"photos"}))
        .WillOnce(::testing::Return(hits({folder("photos", 20)})));
    EXPECT_CALL(*client, findChildFiles(20, false, std::vector<std::string>{"p.jpg"}))
        .WillOnce(::testing::Return(hits({})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadCollision>> result =
        service.findCollisions({"C:\\a\\photos", "C:\\b\\photos"}, 7, false);

    ASSERT_TRUE(result.success);
    ASSERT_EQ(result.value().size(), 1u);
    EXPECT_EQ(result.value()[0].localPath, "C:\\b\\photos\\p.jpg");
    EXPECT_EQ(result.value()[0].existingHandle, 0u);
    EXPECT_EQ(result.value()[0].parentHandle, 20u);
}

TEST(UploadScanServiceTest, PlanRefusesToTakeApartAFolderTheUploadCreatesItself)
{
    // A plan item names its parent by handle, and the folder holding q.jpg does not
    // have one yet. Sending the second copy whole would version over what Skip was
    // asked to spare, so the plan fails instead.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addDirectory("C:\\a", "photos");
    fs->addFile("C:\\a\\photos", "p.jpg");
    fs->addDirectory("C:\\b", "photos");
    fs->addFile("C:\\b\\photos", "p.jpg");
    fs->addFile("C:\\b\\photos", "q.jpg");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFolders(7, false, std::vector<std::string>{"photos"}))
        .WillOnce(::testing::Return(hits({})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadPlanItem>> plan =
        service.planSkippingCollisions({"C:\\a\\photos", "C:\\b\\photos"}, 7, false);

    EXPECT_FALSE(plan.success);
    EXPECT_EQ(plan.errorCode, MegaErrorCode::kEInternal);
}

TEST(UploadScanServiceTest, PlanLeavesOutAWholeSecondCopyOfANewFolderWhenAllOfItCollides)
{
    // The one shape Skip can still express without a handle: nothing in the second
    // copy survives, so leaving the copy out entirely is the whole plan for it.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addDirectory("C:\\a", "photos");
    fs->addFile("C:\\a\\photos", "p.jpg");
    fs->addDirectory("C:\\b", "photos");
    fs->addFile("C:\\b\\photos", "p.jpg");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFolders(7, false, std::vector<std::string>{"photos"}))
        .WillOnce(::testing::Return(hits({})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadPlanItem>> plan =
        service.planSkippingCollisions({"C:\\a\\photos", "C:\\b\\photos"}, 7, false);

    ASSERT_TRUE(plan.success);
    ASSERT_EQ(plan.value().size(), 1u);
    EXPECT_EQ(plan.value()[0].localPath, "C:\\a\\photos");
    EXPECT_EQ(plan.value()[0].parentHandle, 7u);
}

TEST(UploadScanServiceTest, PlanLeavesOutASecondCopyWhoseSubfolderTheFirstCopyBringsToo)
{
    // The redundancy can sit below the copy as well: `empty` is not a collision --
    // no file lands on anything -- yet the first copy already brings it, so the
    // second copy still has nothing of its own and is dropped rather than refused.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addDirectory("C:\\a", "photos");
    fs->addFile("C:\\a\\photos", "p.jpg");
    fs->addDirectory("C:\\a\\photos", "empty");
    fs->addDirectory("C:\\b", "photos");
    fs->addFile("C:\\b\\photos", "p.jpg");
    fs->addDirectory("C:\\b\\photos", "empty");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFolders(7, false, std::vector<std::string>{"photos"}))
        .WillOnce(::testing::Return(hits({})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadPlanItem>> plan =
        service.planSkippingCollisions({"C:\\a\\photos", "C:\\b\\photos"}, 7, false);

    ASSERT_TRUE(plan.success);
    ASSERT_EQ(plan.value().size(), 1u);
    EXPECT_EQ(plan.value()[0].localPath, "C:\\a\\photos");
}

TEST(UploadScanServiceTest, PlanRefusesWhenOnlyTheSecondCopyBringsASubfolder)
{
    // The other side of the same question: `extra` is nobody's duplicate, so it has
    // to be uploaded -- and there is no handle to upload it under.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addDirectory("C:\\a", "photos");
    fs->addFile("C:\\a\\photos", "p.jpg");
    fs->addDirectory("C:\\b", "photos");
    fs->addFile("C:\\b\\photos", "p.jpg");
    fs->addDirectory("C:\\b\\photos", "extra");
    fs->addFile("C:\\b\\photos\\extra", "z.jpg");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFolders(7, false, std::vector<std::string>{"photos"}))
        .WillOnce(::testing::Return(hits({})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadPlanItem>> plan =
        service.planSkippingCollisions({"C:\\a\\photos", "C:\\b\\photos"}, 7, false);

    EXPECT_FALSE(plan.success);
    EXPECT_EQ(plan.errorCode, MegaErrorCode::kEInternal);
}

TEST(UploadScanServiceTest, PlanSendsBothCopiesOfANewFolderWholeWhenNothingInsideOverlaps)
{
    // Descending into a folder the upload creates is only a question. Finding no
    // overlap leaves both copies as ordinary folder transfers, which the SDK merges.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addDirectory("C:\\a", "photos");
    fs->addFile("C:\\a\\photos", "p.jpg");
    fs->addDirectory("C:\\b", "photos");
    fs->addFile("C:\\b\\photos", "q.jpg");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFolders(7, false, std::vector<std::string>{"photos"}))
        .WillRepeatedly(::testing::Return(hits({})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadPlanItem>> plan =
        service.planSkippingCollisions({"C:\\a\\photos", "C:\\b\\photos"}, 7, false);

    ASSERT_TRUE(plan.success);
    ASSERT_EQ(plan.value().size(), 2u);
    EXPECT_EQ(plan.value()[0].localPath, "C:\\a\\photos");
    EXPECT_EQ(plan.value()[0].parentHandle, 7u);
    EXPECT_EQ(plan.value()[1].localPath, "C:\\b\\photos");
}

TEST(UploadScanServiceTest, DoesNotCountAFileAndAFolderSharingAName)
{
    // Matched by kind, as on the copy/move path: a folder cannot version over a
    // file, so pairing them would ask a question with no useful answer.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addFile("C:\\a", "x");
    fs->addDirectory("C:\\b", "x");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFiles(7, false, std::vector<std::string>{"x"}))
        .WillOnce(::testing::Return(hits({})));
    EXPECT_CALL(*client, findChildFolders(7, false, std::vector<std::string>{"x"}))
        .WillOnce(::testing::Return(hits({})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadCollision>> result =
        service.findCollisions({"C:\\a\\x", "C:\\b\\x"}, 7, false);

    ASSERT_TRUE(result.success);
    EXPECT_TRUE(result.value().empty());
}

TEST(UploadScanServiceTest, TreatsTheSamePathListedTwiceAsOneUpload)
{
    // Not a duplicate name -- the same file. Counting it against itself would put
    // its path in the skip set, and the plan keys on path, so it would go up zero
    // times instead of once.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addFile("C:\\a", "x.txt");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFiles(7, false, std::vector<std::string>{"x.txt"}))
        .WillRepeatedly(::testing::Return(hits({})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadCollision>> result =
        service.findCollisions({"C:\\a\\x.txt", "C:\\a\\x.txt"}, 7, false);
    ASSERT_TRUE(result.success);
    EXPECT_TRUE(result.value().empty());

    Result<std::vector<UploadPlanItem>> plan =
        service.planSkippingCollisions({"C:\\a\\x.txt", "C:\\a\\x.txt"}, 7, false);
    ASSERT_TRUE(plan.success);
    ASSERT_EQ(plan.value().size(), 1u);
    EXPECT_EQ(plan.value()[0].localPath, "C:\\a\\x.txt");
}

TEST(UploadScanServiceTest, TreatsTwoSpellingsOfOnePathAsOneUpload)
{
    // The de-duplication has to key on the resolved path, not on what the caller
    // typed: everything downstream keys on the resolved one, so two spellings that
    // survive would both be dropped from the plan rather than one.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addFile("C:\\a", "x.txt");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFiles(7, false, std::vector<std::string>{"x.txt"}))
        .WillRepeatedly(::testing::Return(hits({})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadCollision>> result =
        service.findCollisions({"C:\\a\\x.txt", "C:/a/x.txt"}, 7, false);
    ASSERT_TRUE(result.success);
    EXPECT_TRUE(result.value().empty());

    Result<std::vector<UploadPlanItem>> plan =
        service.planSkippingCollisions({"C:\\a\\x.txt", "C:/a/x.txt"}, 7, false);
    ASSERT_TRUE(plan.success);
    ASSERT_EQ(plan.value().size(), 1u);
    EXPECT_EQ(plan.value()[0].localPath, "C:\\a\\x.txt");
}

TEST(UploadScanServiceTest, PlanKeepsTheFirstCopyOfANameTheUploadBringsTwice)
{
    // Skip has to leave one behind: dropping both would lose a file the user never
    // declined, and keeping both is what they just declined.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addFile("C:\\a", "x.txt");
    fs->addFile("C:\\b", "x.txt");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFiles(7, false, std::vector<std::string>{"x.txt"}))
        .WillOnce(::testing::Return(hits({})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadPlanItem>> plan =
        service.planSkippingCollisions({"C:\\a\\x.txt", "C:\\b\\x.txt"}, 7, false);

    ASSERT_TRUE(plan.success);
    ASSERT_EQ(plan.value().size(), 1u);
    EXPECT_EQ(plan.value()[0].localPath, "C:\\a\\x.txt");
}

TEST(UploadScanServiceTest, PlanLeavesOutEveryCopyOfACollidingName)
{
    // The defect this guards: dropping two files that share a leaf name used to
    // produce one collision row, so Skip spared one and versioned over the node
    // with the other.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addFile("C:\\a", "x.txt");
    fs->addFile("C:\\b", "x.txt");
    fs->addFile("C:\\a", "y.txt");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFiles(7, false, std::vector<std::string>{"x.txt", "y.txt"}))
        .WillOnce(::testing::Return(hits({file("x.txt", 31)})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadPlanItem>> plan =
        service.planSkippingCollisions({"C:\\a\\x.txt", "C:\\b\\x.txt", "C:\\a\\y.txt"}, 7, false);

    ASSERT_TRUE(plan.success);
    ASSERT_EQ(plan.value().size(), 1u);
    EXPECT_EQ(plan.value()[0].localPath, "C:\\a\\y.txt");
}

TEST(UploadScanServiceTest, PlanTakesApartEveryCopyOfAMergingFolderName)
{
    // The folder half of the same defect, and the worse one: an undescended twin
    // went to the SDK whole, which merges recursively and versions over the very
    // files the dialog had just listed.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addDirectory("C:\\a", "photos");
    fs->addFile("C:\\a\\photos", "p.jpg");
    fs->addDirectory("C:\\b", "photos");
    fs->addFile("C:\\b\\photos", "p.jpg");
    fs->addFile("C:\\b\\photos", "q.jpg");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFolders(7, false, std::vector<std::string>{"photos"}))
        .WillOnce(::testing::Return(hits({folder("photos", 20)})));
    // One lookup, not one per copy: both copies become the same MEGA folder, so
    // their children are walked as a single level.
    EXPECT_CALL(*client, findChildFiles(20, false, std::vector<std::string>{"p.jpg", "q.jpg"}))
        .WillOnce(::testing::Return(hits({file("p.jpg", 55)})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadPlanItem>> plan =
        service.planSkippingCollisions({"C:\\a\\photos", "C:\\b\\photos"}, 7, false);

    ASSERT_TRUE(plan.success);
    ASSERT_EQ(plan.value().size(), 1u);
    EXPECT_EQ(plan.value()[0].localPath, "C:\\b\\photos\\q.jpg");
    EXPECT_EQ(plan.value()[0].parentHandle, 20u);
}

TEST(UploadScanServiceTest, PlanSendsAnUntouchedBranchWholeAndWalksOnlyTheCollidingOne)
{
    // The shape spec 5-2 asks for: one SDK folder transfer wherever nothing inside
    // collides, individual files only along the branch that does.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addDirectory("C:\\src", "dir");
    fs->addFile("C:\\src\\dir", "a.txt");
    fs->addFile("C:\\src\\dir", "b.txt");
    fs->addDirectory("C:\\src\\dir", "sub");
    fs->addFile("C:\\src\\dir\\sub", "c.txt");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFolders(7, false, std::vector<std::string>{"dir"}))
        .WillOnce(::testing::Return(hits({folder("dir", 20)})));
    EXPECT_CALL(*client, findChildFiles(20, false, std::vector<std::string>{"a.txt", "b.txt"}))
        .WillOnce(::testing::Return(hits({file("a.txt", 55)})));
    EXPECT_CALL(*client, findChildFolders(20, false, std::vector<std::string>{"sub"}))
        .WillOnce(::testing::Return(hits({})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadPlanItem>> plan =
        service.planSkippingCollisions({"C:\\src\\dir"}, 7, false);

    ASSERT_TRUE(plan.success);
    ASSERT_EQ(plan.value().size(), 2u);
    EXPECT_EQ(plan.value()[0].localPath, "C:\\src\\dir\\b.txt");
    EXPECT_EQ(plan.value()[0].parentHandle, 20u);
    EXPECT_FALSE(plan.value()[0].parentIsRoot);
    // Not c.txt: the subfolder holds no collision, so it goes as one transfer.
    EXPECT_EQ(plan.value()[1].localPath, "C:\\src\\dir\\sub");
    EXPECT_EQ(plan.value()[1].parentHandle, 20u);
}

TEST(UploadScanServiceTest, PlanKeepsAMergingFolderWholeWhenNothingInsideCollides)
{
    // Descending is not the same as taking apart: MEGA having a folder of that name
    // is only the reason to look, and finding nothing means the SDK can still do it
    // in one transfer.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addDirectory("C:\\src", "dir");
    fs->addFile("C:\\src\\dir", "file1.txt");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFolders(7, false, std::vector<std::string>{"dir"}))
        .WillOnce(::testing::Return(hits({folder("dir", 20)})));
    EXPECT_CALL(*client, findChildFiles(20, false, std::vector<std::string>{"file1.txt"}))
        .WillOnce(::testing::Return(hits({})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadPlanItem>> plan =
        service.planSkippingCollisions({"C:\\src\\dir"}, 7, false);

    ASSERT_TRUE(plan.success);
    ASSERT_EQ(plan.value().size(), 1u);
    EXPECT_EQ(plan.value()[0].localPath, "C:\\src\\dir");
    EXPECT_EQ(plan.value()[0].parentHandle, 7u);
}

TEST(UploadScanServiceTest, PlanFailsRatherThanUnderReportWhenTheLookupFails)
{
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addFile("C:\\src", "a.txt");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFiles(7, false, std::vector<std::string>{"a.txt"}))
        .WillOnce(::testing::Return(
            Result<std::vector<FileEntry>>::fail("gone", MegaErrorCode::kENoEnt)));

    UploadScanService service(client, fs);

    Result<std::vector<UploadPlanItem>> plan =
        service.planSkippingCollisions({"C:\\src\\a.txt"}, 7, false);

    EXPECT_FALSE(plan.success);
    EXPECT_EQ(plan.errorCode, MegaErrorCode::kENoEnt);
}

TEST(UploadScanServiceTest, PlanFailsRatherThanDropSurvivorsWhenABranchStopsListing)
{
    // The folder is taken apart because something inside it collides, so a listing
    // that now refuses would leave b.txt out of the plan with nothing said -- the
    // user asked to skip one file, not to lose the other.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addDirectory("C:\\src", "dir");
    fs->addFile("C:\\src\\dir", "a.txt");
    fs->addFile("C:\\src\\dir", "b.txt");
    fs->unreadableOnRelisting.insert("C:\\src\\dir");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFolders(7, false, std::vector<std::string>{"dir"}))
        .WillOnce(::testing::Return(hits({folder("dir", 20)})));
    EXPECT_CALL(*client, findChildFiles(20, false, std::vector<std::string>{"a.txt", "b.txt"}))
        .WillOnce(::testing::Return(hits({file("a.txt", 55)})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadPlanItem>> plan =
        service.planSkippingCollisions({"C:\\src\\dir"}, 7, false);

    EXPECT_FALSE(plan.success);
    EXPECT_EQ(plan.errorCode, MegaErrorCode::kEInternal);
}

TEST(UploadScanServiceTest, ScanWalksOnPastAFolderThatWillNotList)
{
    // The other half of the same rule: the scan finds no collision inside, so the
    // plan hands the folder to the SDK whole and nothing is dropped -- costing a
    // question rather than a failure.
    auto fs = std::make_shared<FakeLocalFileSystem>();
    fs->addDirectory("C:\\src", "dir");
    fs->addFile("C:\\src\\dir", "a.txt");
    fs->unreadable.insert("C:\\src\\dir");

    auto client = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*client, findChildFolders(7, false, std::vector<std::string>{"dir"}))
        .WillRepeatedly(::testing::Return(hits({folder("dir", 20)})));

    UploadScanService service(client, fs);

    Result<std::vector<UploadCollision>> found = service.findCollisions({"C:\\src\\dir"}, 7, false);
    ASSERT_TRUE(found.success);
    EXPECT_TRUE(found.value().empty());

    Result<std::vector<UploadPlanItem>> plan =
        service.planSkippingCollisions({"C:\\src\\dir"}, 7, false);
    ASSERT_TRUE(plan.success);
    ASSERT_EQ(plan.value().size(), 1u);
    EXPECT_EQ(plan.value()[0].localPath, "C:\\src\\dir");
    EXPECT_EQ(plan.value()[0].parentHandle, 7u);
}
