#include "core/QuickAccessService.h"

#include "core/MegaErrorCodes.h"
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
        // Fixed default account for tests that don't care about account
        // scoping (Phase 11a); PinsAreIsolatedPerAccount below overrides this.
        ON_CALL(*client, currentUserHandle()).WillByDefault(Return(Result<std::uint64_t>::ok(111)));
        service = std::make_unique<QuickAccessService>(client, store);
    }

    // The common case: nothing persisted yet, and saves succeed. Individual
    // tests override the expectation they actually care about.
    void expectEmptyStore()
    {
        EXPECT_CALL(*store, load(_))
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
    EXPECT_CALL(*store, load(_)).WillOnce(Return(Result<std::vector<PinnedFolder>>::ok(stored)));

    EXPECT_TRUE(service->load().success);

    EXPECT_EQ(service->pins(), stored);
}

TEST_F(QuickAccessServiceTest, LoadFailureDegradesToEmptyList)
{
    EXPECT_CALL(*store, load(_))
        .WillOnce(
            Return(Result<std::vector<PinnedFolder>>::fail("corrupt", MegaErrorCode::kEInternal)));

    // Degrades to empty, but the caller can still tell why: QuickAccessModel
    // is what logs it, since this class is Qt-free.
    const Result<void> loaded = service->load();
    EXPECT_FALSE(loaded.success);
    EXPECT_EQ(loaded.errorCode, MegaErrorCode::kEInternal);
    EXPECT_EQ(loaded.errorMessage, "corrupt");

    EXPECT_TRUE(service->pins().empty());
}

TEST_F(QuickAccessServiceTest, PinAppendsAtTheEndAndPersists)
{
    expectEmptyStore();
    EXPECT_TRUE(service->load().success);

    const std::vector<PinnedFolder> afterFirst = {makePin("Photos", 11)};
    const std::vector<PinnedFolder> afterSecond = {makePin("Photos", 11), makePin("Work", 22)};
    EXPECT_CALL(*store, save(_, afterFirst)).WillOnce(Return(Result<void>::ok()));
    EXPECT_CALL(*store, save(_, afterSecond)).WillOnce(Return(Result<void>::ok()));

    EXPECT_TRUE(service->pin(makePin("Photos", 11)));
    EXPECT_TRUE(service->pin(makePin("Work", 22)));

    EXPECT_EQ(service->pins(), afterSecond);
}

TEST_F(QuickAccessServiceTest, PinRejectsADuplicateHandleWithoutSaving)
{
    expectEmptyStore();
    EXPECT_TRUE(service->load().success);

    // Exactly one save: the second pin() must not reach the store at all.
    EXPECT_CALL(*store, save(_, _)).Times(1).WillRepeatedly(Return(Result<void>::ok()));

    EXPECT_TRUE(service->pin(makePin("Photos", 11)));
    EXPECT_FALSE(service->pin(makePin("Photos renamed elsewhere", 11)));

    ASSERT_EQ(service->pins().size(), 1u);
    EXPECT_EQ(service->pins()[0].name, "Photos");
}

TEST_F(QuickAccessServiceTest, UnpinRemovesAndPersists)
{
    EXPECT_CALL(*store, load(_))
        .WillOnce(Return(
            Result<std::vector<PinnedFolder>>::ok({makePin("Photos", 11), makePin("Work", 22)})));
    EXPECT_TRUE(service->load().success);

    const std::vector<PinnedFolder> remaining = {makePin("Work", 22)};
    EXPECT_CALL(*store, save(_, remaining)).WillOnce(Return(Result<void>::ok()));

    EXPECT_TRUE(service->unpin(11));
    EXPECT_EQ(service->pins(), remaining);
}

TEST_F(QuickAccessServiceTest, UnpinOfAnUnknownHandleDoesNothing)
{
    expectEmptyStore();
    EXPECT_TRUE(service->load().success);

    EXPECT_CALL(*store, save(_, _)).Times(0);

    EXPECT_FALSE(service->unpin(99));
}

TEST_F(QuickAccessServiceTest, MoveReordersAndPersists)
{
    EXPECT_CALL(*store, load(_))
        .WillOnce(Return(Result<std::vector<PinnedFolder>>::ok(
            {makePin("Photos", 11), makePin("Work", 22), makePin("Docs", 33)})));
    EXPECT_TRUE(service->load().success);

    // Downwards: the pins between source and destination shift up by one.
    const std::vector<PinnedFolder> afterDown = {
        makePin("Work", 22), makePin("Docs", 33), makePin("Photos", 11)};
    EXPECT_CALL(*store, save(_, afterDown)).WillOnce(Return(Result<void>::ok()));
    EXPECT_TRUE(service->move(0, 2));
    EXPECT_EQ(service->pins(), afterDown);

    // Upwards, back to the original order.
    const std::vector<PinnedFolder> afterUp = {
        makePin("Photos", 11), makePin("Work", 22), makePin("Docs", 33)};
    EXPECT_CALL(*store, save(_, afterUp)).WillOnce(Return(Result<void>::ok()));
    EXPECT_TRUE(service->move(2, 0));
    EXPECT_EQ(service->pins(), afterUp);
}

TEST_F(QuickAccessServiceTest, MoveRejectsAnOutOfRangeOrNoOpIndexWithoutSaving)
{
    const std::vector<PinnedFolder> stored = {makePin("Photos", 11), makePin("Work", 22)};
    EXPECT_CALL(*store, load(_)).WillOnce(Return(Result<std::vector<PinnedFolder>>::ok(stored)));
    EXPECT_TRUE(service->load().success);

    EXPECT_CALL(*store, save(_, _)).Times(0);

    EXPECT_FALSE(service->move(0, 2));
    EXPECT_FALSE(service->move(5, 0));
    EXPECT_FALSE(service->move(1, 1));
    EXPECT_EQ(service->pins(), stored);
}

TEST_F(QuickAccessServiceTest, IsPinnedTracksTheCurrentList)
{
    EXPECT_CALL(*store, load(_))
        .WillOnce(Return(Result<std::vector<PinnedFolder>>::ok({makePin("Photos", 11)})));
    EXPECT_CALL(*store, save(_, _)).WillRepeatedly(Return(Result<void>::ok()));
    EXPECT_TRUE(service->load().success);

    EXPECT_TRUE(service->isPinned(11));
    EXPECT_FALSE(service->isPinned(22));

    service->unpin(11);
    EXPECT_FALSE(service->isPinned(11));
}

TEST_F(QuickAccessServiceTest, ReplaceAllOverwritesAndPersistsInOneWrite)
{
    EXPECT_CALL(*store, load(_))
        .WillOnce(Return(
            Result<std::vector<PinnedFolder>>::ok({makePin("Photos", 11), makePin("Work", 22)})));
    EXPECT_TRUE(service->load().success);

    const std::vector<PinnedFolder> replacement = {makePin("Photos (renamed)", 11)};
    EXPECT_CALL(*store, save(_, replacement)).Times(1).WillOnce(Return(Result<void>::ok()));

    service->replaceAll(replacement);

    EXPECT_EQ(service->pins(), replacement);
}

// R3-3: a failed write-through used to be discarded outright, so a pin change
// silently didn't survive the next restart. Every mutator writes through the
// same persist() helper, so one failing store covers all four.
TEST_F(QuickAccessServiceTest, PersistenceFailureReachesTheHandlerFromEveryMutator)
{
    EXPECT_CALL(*store, load(_))
        .WillOnce(Return(
            Result<std::vector<PinnedFolder>>::ok({makePin("Photos", 11), makePin("Work", 22)})));
    EXPECT_TRUE(service->load().success);

    std::vector<Result<void>> failures;
    service->setOnPersistenceFailed([&failures](const Result<void>& failure) {
        failures.push_back(failure);
    });

    EXPECT_CALL(*store, save(_, _))
        .WillRepeatedly(Return(
            Result<void>::fail("failed to save quick-access pins", MegaErrorCode::kEInternal)));

    // The in-memory list is still updated, so each mutator still reports
    // success -- the bool answers "was the change accepted", not "was it
    // saved".
    EXPECT_TRUE(service->pin(makePin("Vacation", 33)));
    EXPECT_TRUE(service->unpin(11));
    EXPECT_TRUE(service->move(0, 1));
    service->replaceAll({makePin("Photos", 11)});

    ASSERT_EQ(failures.size(), 4u);
    for (const Result<void>& failure : failures)
    {
        EXPECT_FALSE(failure.success);
        EXPECT_EQ(failure.errorCode, MegaErrorCode::kEInternal);
        EXPECT_EQ(failure.errorMessage, "failed to save quick-access pins");
    }
}

TEST_F(QuickAccessServiceTest, PersistenceSuccessDoesNotCallTheHandler)
{
    expectEmptyStore();
    EXPECT_TRUE(service->load().success);

    int calls = 0;
    service->setOnPersistenceFailed([&calls](const Result<void>&) {
        ++calls;
    });

    EXPECT_CALL(*store, save(_, _)).WillRepeatedly(Return(Result<void>::ok()));

    EXPECT_TRUE(service->pin(makePin("Photos", 11)));
    EXPECT_TRUE(service->unpin(11));

    EXPECT_EQ(calls, 0);
}

// A rejected mutation writes nothing, so it can't report a write failure
// either -- otherwise a duplicate pin would raise a "couldn't save" toast.
TEST_F(QuickAccessServiceTest, RejectedMutationsNeverReachTheHandler)
{
    EXPECT_CALL(*store, load(_))
        .WillOnce(Return(Result<std::vector<PinnedFolder>>::ok({makePin("Photos", 11)})));
    EXPECT_TRUE(service->load().success);

    int calls = 0;
    service->setOnPersistenceFailed([&calls](const Result<void>&) {
        ++calls;
    });

    EXPECT_CALL(*store, save(_, _)).Times(0);

    EXPECT_FALSE(service->pin(makePin("Photos again", 11)));
    EXPECT_FALSE(service->unpin(99));
    EXPECT_FALSE(service->move(0, 0));

    EXPECT_EQ(calls, 0);
}

TEST_F(QuickAccessServiceTest, ClearEmptiesMemoryButLeavesTheStoreAlone)
{
    EXPECT_CALL(*store, load(_))
        .WillOnce(Return(Result<std::vector<PinnedFolder>>::ok({makePin("Photos", 11)})));
    EXPECT_TRUE(service->load().success);

    // Sign-out must not wipe the persisted list -- signing back in restores it.
    EXPECT_CALL(*store, save(_, _)).Times(0);

    service->clear();

    EXPECT_TRUE(service->pins().empty());
}

// Phase 11a: pins are scoped by account, so signing into a different account
// must not see (or silently overwrite) the previous account's pins.
TEST_F(QuickAccessServiceTest, PinsAreIsolatedPerAccount)
{
    EXPECT_CALL(*client, currentUserHandle())
        .WillOnce(Return(Result<std::uint64_t>::ok(111)))
        .WillOnce(Return(Result<std::uint64_t>::ok(222)));
    EXPECT_CALL(*store, load("111"))
        .WillOnce(Return(Result<std::vector<PinnedFolder>>::ok({makePin("Photos", 11)})));
    EXPECT_CALL(*store, load("222")).WillOnce(Return(Result<std::vector<PinnedFolder>>::ok({})));

    EXPECT_TRUE(service->load().success); // account 111
    EXPECT_EQ(service->pins(), std::vector<PinnedFolder>{makePin("Photos", 11)});

    service->clear();                     // sign-out
    EXPECT_TRUE(service->load().success); // account 222 -- must not inherit account 111's pins
    EXPECT_TRUE(service->pins().empty());

    EXPECT_CALL(*store, save("222", _)).Times(1).WillOnce(Return(Result<void>::ok()));
    EXPECT_TRUE(service->pin(makePin("Vacation", 33)));
}

// Phase 11a: with no resolvable account, load() must degrade the same way a
// store failure already does, and mutators must not persist under a bogus key.
TEST_F(QuickAccessServiceTest, LoadWithNoLoggedInAccountDegradesToEmptyAndSkipsTheStore)
{
    EXPECT_CALL(*client, currentUserHandle())
        .WillOnce(Return(Result<std::uint64_t>::fail("not logged in", MegaErrorCode::kEInternal)));
    EXPECT_CALL(*store, load(_)).Times(0);

    // The client's own failure is forwarded verbatim, so the caller can tell
    // "not signed in" from a store error.
    const Result<void> loaded = service->load();
    EXPECT_FALSE(loaded.success);
    EXPECT_EQ(loaded.errorMessage, "not logged in");

    EXPECT_TRUE(service->pins().empty());

    EXPECT_CALL(*store, save(_, _)).Times(0);
    EXPECT_TRUE(service->pin(makePin("Photos", 11)));
}

TEST_F(QuickAccessServiceTest, ResolveFolderDelegatesToGetNodeInfo)
{
    const NodeInfo info = makeNodeInfo("Photos", 11, true, true);
    EXPECT_CALL(*client, getNodeInfo(11u, _))
        .WillOnce(InvokeArgument<1>(Result<NodeInfo>::ok(info)));

    Result<NodeInfo> received = Result<NodeInfo>::fail("not called", MegaErrorCode::kEInternal);
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
    EXPECT_FALSE(QuickAccessService::isUsable(
        Result<NodeInfo>::fail("no such node", MegaErrorCode::kENoEnt)));
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
