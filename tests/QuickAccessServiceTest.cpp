#include "core/QuickAccessService.h"

#include "MockMegaClient.h"
#include "MockPinnedFolderStore.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::_;
using ::testing::InvokeArgument;
using ::testing::Return;

namespace
{

PinnedFolder makePin(const char* name, std::uint64_t handle)
{
    PinnedFolder pin;
    pin.name = name;
    pin.handle = handle;
    return pin;
}

NodeInfo makeNodeInfo(const char* name, std::uint64_t handle, bool isFolder, bool inCloud)
{
    NodeInfo info;
    info.name = name;
    info.handle = handle;
    info.isFolder = isFolder;
    info.inCloud = inCloud;
    return info;
}

class QuickAccessServiceTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        client = std::make_shared<MockMegaClient>();
        store = std::make_shared<MockPinnedFolderStore>();
        service = std::make_unique<QuickAccessService>(client, store);
    }

    // The common case: nothing persisted yet, and saves succeed. Individual
    // tests override the expectation they actually care about.
    void expectEmptyStore()
    {
        EXPECT_CALL(*store, load())
            .WillRepeatedly(Return(Result<std::vector<PinnedFolder>>::ok({})));
    }

    std::shared_ptr<MockMegaClient> client;
    std::shared_ptr<MockPinnedFolderStore> store;
    std::unique_ptr<QuickAccessService> service;
};

} // namespace

TEST_F(QuickAccessServiceTest, LoadReflectsStoredPinsInOrder)
{
    const std::vector<PinnedFolder> stored = {makePin("Photos", 11), makePin("Work", 22)};
    EXPECT_CALL(*store, load()).WillOnce(Return(Result<std::vector<PinnedFolder>>::ok(stored)));

    service->load();

    EXPECT_EQ(service->pins(), stored);
}

TEST_F(QuickAccessServiceTest, LoadFailureDegradesToEmptyList)
{
    EXPECT_CALL(*store, load())
        .WillOnce(Return(Result<std::vector<PinnedFolder>>::fail("corrupt")));

    service->load();

    EXPECT_TRUE(service->pins().empty());
}

TEST_F(QuickAccessServiceTest, PinAppendsAtTheEndAndPersists)
{
    expectEmptyStore();
    service->load();

    const std::vector<PinnedFolder> afterFirst = {makePin("Photos", 11)};
    const std::vector<PinnedFolder> afterSecond = {makePin("Photos", 11), makePin("Work", 22)};
    EXPECT_CALL(*store, save(afterFirst)).WillOnce(Return(Result<void>::ok()));
    EXPECT_CALL(*store, save(afterSecond)).WillOnce(Return(Result<void>::ok()));

    EXPECT_TRUE(service->pin(makePin("Photos", 11)));
    EXPECT_TRUE(service->pin(makePin("Work", 22)));

    EXPECT_EQ(service->pins(), afterSecond);
}

TEST_F(QuickAccessServiceTest, PinRejectsADuplicateHandleWithoutSaving)
{
    expectEmptyStore();
    service->load();

    // Exactly one save: the second pin() must not reach the store at all.
    EXPECT_CALL(*store, save(_)).Times(1).WillRepeatedly(Return(Result<void>::ok()));

    EXPECT_TRUE(service->pin(makePin("Photos", 11)));
    EXPECT_FALSE(service->pin(makePin("Photos renamed elsewhere", 11)));

    ASSERT_EQ(service->pins().size(), 1u);
    EXPECT_EQ(service->pins()[0].name, "Photos");
}

TEST_F(QuickAccessServiceTest, UnpinRemovesAndPersists)
{
    EXPECT_CALL(*store, load())
        .WillOnce(Return(
            Result<std::vector<PinnedFolder>>::ok({makePin("Photos", 11), makePin("Work", 22)})));
    service->load();

    const std::vector<PinnedFolder> remaining = {makePin("Work", 22)};
    EXPECT_CALL(*store, save(remaining)).WillOnce(Return(Result<void>::ok()));

    EXPECT_TRUE(service->unpin(11));
    EXPECT_EQ(service->pins(), remaining);
}

TEST_F(QuickAccessServiceTest, UnpinOfAnUnknownHandleDoesNothing)
{
    expectEmptyStore();
    service->load();

    EXPECT_CALL(*store, save(_)).Times(0);

    EXPECT_FALSE(service->unpin(99));
}

TEST_F(QuickAccessServiceTest, IsPinnedTracksTheCurrentList)
{
    EXPECT_CALL(*store, load())
        .WillOnce(Return(Result<std::vector<PinnedFolder>>::ok({makePin("Photos", 11)})));
    EXPECT_CALL(*store, save(_)).WillRepeatedly(Return(Result<void>::ok()));
    service->load();

    EXPECT_TRUE(service->isPinned(11));
    EXPECT_FALSE(service->isPinned(22));

    service->unpin(11);
    EXPECT_FALSE(service->isPinned(11));
}

TEST_F(QuickAccessServiceTest, ReplaceAllOverwritesAndPersistsInOneWrite)
{
    EXPECT_CALL(*store, load())
        .WillOnce(Return(
            Result<std::vector<PinnedFolder>>::ok({makePin("Photos", 11), makePin("Work", 22)})));
    service->load();

    const std::vector<PinnedFolder> replacement = {makePin("Photos (renamed)", 11)};
    EXPECT_CALL(*store, save(replacement)).Times(1).WillOnce(Return(Result<void>::ok()));

    service->replaceAll(replacement);

    EXPECT_EQ(service->pins(), replacement);
}

TEST_F(QuickAccessServiceTest, ClearEmptiesMemoryButLeavesTheStoreAlone)
{
    EXPECT_CALL(*store, load())
        .WillOnce(Return(Result<std::vector<PinnedFolder>>::ok({makePin("Photos", 11)})));
    service->load();

    // Sign-out must not wipe the persisted list -- signing back in restores it.
    EXPECT_CALL(*store, save(_)).Times(0);

    service->clear();

    EXPECT_TRUE(service->pins().empty());
}

TEST_F(QuickAccessServiceTest, ResolveFolderDelegatesToGetNodeInfo)
{
    const NodeInfo info = makeNodeInfo("Photos", 11, true, true);
    EXPECT_CALL(*client, getNodeInfo(11u, _))
        .WillOnce(InvokeArgument<1>(Result<NodeInfo>::ok(info)));

    Result<NodeInfo> received = Result<NodeInfo>::fail("not called");
    service->resolveFolder(11, [&received](Result<NodeInfo> result) {
        received = std::move(result);
    });

    ASSERT_TRUE(received.success);
    EXPECT_EQ(received.value, info);
}

// isUsable is the single definition of "this pin still points at something
// usable", shared by the login-time sweep and the click-time check, so each
// way it can say no is pinned down here.
TEST(QuickAccessServiceIsUsableTest, AcceptsALiveCloudDriveFolder)
{
    EXPECT_TRUE(
        QuickAccessService::isUsable(Result<NodeInfo>::ok(makeNodeInfo("Photos", 11, true, true))));
}

TEST(QuickAccessServiceIsUsableTest, RejectsAnUnresolvableHandle)
{
    EXPECT_FALSE(QuickAccessService::isUsable(Result<NodeInfo>::fail("no such node")));
}

TEST(QuickAccessServiceIsUsableTest, RejectsANodeOutsideTheCloudDrive)
{
    // A MEGA delete only moves the node to the Rubbish bin, so it still
    // resolves -- inCloud is what rules it out.
    EXPECT_FALSE(QuickAccessService::isUsable(
        Result<NodeInfo>::ok(makeNodeInfo("Photos", 11, true, false))));
}

TEST(QuickAccessServiceIsUsableTest, RejectsAFile)
{
    EXPECT_FALSE(QuickAccessService::isUsable(
        Result<NodeInfo>::ok(makeNodeInfo("photo.jpg", 11, false, true))));
}
