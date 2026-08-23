#include "core/NodeDetailsService.h"

#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace
{

// The shape MegaSdkClient::getPath hands back: root first, the node itself last.
std::vector<PathSegment> pathOf(const std::vector<std::string>& names,
                                ViewKind kind = ViewKind::CloudDrive)
{
    std::vector<PathSegment> segments;
    segments.push_back(PathSegment{"", 0, true, kind});
    for (const std::string& name : names)
        segments.push_back(PathSegment{name, 7, false, kind});
    return segments;
}

struct Captured
{
    bool called = false;
    Result<NodeDetails> result;
};

std::function<void(Result<NodeDetails>)> captureInto(Captured& captured)
{
    return [&captured](Result<NodeDetails> result) {
        captured.called = true;
        captured.result = std::move(result);
    };
}

} // namespace

TEST(NodeDetailsServiceTest, DropsBothTheRootAndTheNodeItselfFromTheParentPath)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getPath(42u, false, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<std::vector<PathSegment>>::ok(pathOf({"photos", "2026", "a.jpg"}))));

    NodeDetailsService service(mockClient);
    Captured captured;

    service.loadDetails(42, false, false, captureInto(captured));

    ASSERT_TRUE(captured.called);
    ASSERT_TRUE(captured.result.success);
    EXPECT_EQ(captured.result.value().parentPath, "/photos/2026");
    EXPECT_FALSE(captured.result.value().hasContents);
}

TEST(NodeDetailsServiceTest, LeavesTheParentPathEmptyForANodeDirectlyUnderTheRoot)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getPath(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(
            ::testing::InvokeArgument<2>(Result<std::vector<PathSegment>>::ok(pathOf({"a.jpg"}))));

    NodeDetailsService service(mockClient);
    Captured captured;

    service.loadDetails(42, false, false, captureInto(captured));

    ASSERT_TRUE(captured.result.success);
    EXPECT_EQ(captured.result.value().parentPath, "");
    EXPECT_EQ(captured.result.value().rootKind, ViewKind::CloudDrive);
}

TEST(NodeDetailsServiceTest, ReportsWhichRootTheWalkEndedAt)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getPath(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<std::vector<PathSegment>>::ok(pathOf({"a.jpg"}, ViewKind::Rubbish))));

    NodeDetailsService service(mockClient);
    Captured captured;

    service.loadDetails(42, false, false, captureInto(captured));

    ASSERT_TRUE(captured.result.success);
    EXPECT_EQ(captured.result.value().rootKind, ViewKind::Rubbish);
}

TEST(NodeDetailsServiceTest, AsksForFolderContentsOnlyForAFolder)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getPath(::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(
            ::testing::InvokeArgument<2>(Result<std::vector<PathSegment>>::ok(pathOf({"photos"}))));
    EXPECT_CALL(*mockClient, getFolderInfo(42u, false, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<FolderInfo>::ok(FolderInfo{3, 1, 900})));

    NodeDetailsService service(mockClient);

    Captured folder;
    service.loadDetails(42, false, true, captureInto(folder));
    ASSERT_TRUE(folder.result.success);
    ASSERT_TRUE(folder.result.value().hasContents);
    EXPECT_EQ(folder.result.value().contents.fileCount, 3u);
    EXPECT_EQ(folder.result.value().contents.folderCount, 1u);
    EXPECT_EQ(folder.result.value().contents.sizeBytes, 900u);

    // Second call is a file: the getFolderInfo expectation above allows exactly one
    // call, so a second one fails the test.
    Captured file;
    service.loadDetails(42, false, false, captureInto(file));
    ASSERT_TRUE(file.result.success);
    EXPECT_FALSE(file.result.value().hasContents);
}

TEST(NodeDetailsServiceTest, FailsWithoutAskingForContentsWhenThePathCannotBeResolved)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getPath(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<std::vector<PathSegment>>::fail("gone", MegaErrorCode::kENoEnt)));
    EXPECT_CALL(*mockClient, getFolderInfo(::testing::_, ::testing::_, ::testing::_)).Times(0);

    NodeDetailsService service(mockClient);
    Captured captured;

    service.loadDetails(42, false, true, captureInto(captured));

    ASSERT_TRUE(captured.called);
    EXPECT_FALSE(captured.result.success);
    EXPECT_EQ(captured.result.errorCode, MegaErrorCode::kENoEnt);
}

TEST(NodeDetailsServiceTest, FailsTheWholeCallWhenTheContentsLookupFails)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getPath(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(
            ::testing::InvokeArgument<2>(Result<std::vector<PathSegment>>::ok(pathOf({"photos"}))));
    EXPECT_CALL(*mockClient, getFolderInfo(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(
            ::testing::InvokeArgument<2>(Result<FolderInfo>::fail("no", MegaErrorCode::kEArgs)));

    NodeDetailsService service(mockClient);
    Captured captured;

    service.loadDetails(42, false, true, captureInto(captured));

    ASSERT_TRUE(captured.called);
    EXPECT_FALSE(captured.result.success);
    EXPECT_EQ(captured.result.errorCode, MegaErrorCode::kEArgs);
}

TEST(NodeDetailsServiceTest, PassesIsRootToBothReadsSoARootCanBeInspected)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    // Handle 0 with the flag set is how a root is named -- getPath itself zeroes the
    // root segment's handle, so nothing else points at one.
    EXPECT_CALL(*mockClient, getPath(0u, true, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<PathSegment>>::ok(pathOf({}))));
    EXPECT_CALL(*mockClient, getFolderInfo(0u, true, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<FolderInfo>::ok(FolderInfo{5, 2, 4096})));

    NodeDetailsService service(mockClient);
    Captured captured;

    service.loadDetails(0, true, true, captureInto(captured));

    ASSERT_TRUE(captured.called);
    ASSERT_TRUE(captured.result.success);
    EXPECT_EQ(captured.result.value().parentPath, "");
    EXPECT_TRUE(captured.result.value().hasContents);
    EXPECT_EQ(captured.result.value().contents.fileCount, 5u);
}
