#include "core/FolderTreeService.h"

#include "MockMegaClient.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace
{

struct Captured
{
    bool called = false;
    Result<std::vector<FileEntry>> result;
};

std::function<void(Result<std::vector<FileEntry>>)> captureInto(Captured& captured)
{
    return [&captured](Result<std::vector<FileEntry>> result) {
        captured.called = true;
        captured.result = std::move(result);
    };
}

} // namespace

TEST(FolderTreeServiceTest, RootRequestGoesToGetRootChildren)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> children{{"sub", 1, 0, true, 0}};

    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(children)));
    EXPECT_CALL(*mockClient, getChildren(::testing::_, ::testing::_, ::testing::_)).Times(0);

    FolderTreeService service(mockClient);
    Captured captured;

    service.loadSubfolders(0, true, captureInto(captured));

    ASSERT_TRUE(captured.called);
    EXPECT_TRUE(captured.result.success);
    EXPECT_EQ(captured.result.value.size(), 1u);
}

TEST(FolderTreeServiceTest, NonRootRequestGoesToGetChildrenWithHandle)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> children{{"sub", 2, 0, true, 0}};

    EXPECT_CALL(*mockClient, getChildren(42, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(children)));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_)).Times(0);

    FolderTreeService service(mockClient);
    Captured captured;

    service.loadSubfolders(42, false, captureInto(captured));

    ASSERT_TRUE(captured.called);
    EXPECT_TRUE(captured.result.success);
    EXPECT_EQ(captured.result.value.size(), 1u);
}

TEST(FolderTreeServiceTest, PassesNameAscendingSortOrder)
{
    auto mockClient = std::make_shared<MockMegaClient>();

    EXPECT_CALL(*mockClient,
                getRootChildren(::testing::AllOf(::testing::Field(&SortOrder::key, SortKey::Name),
                                                 ::testing::Field(&SortOrder::ascending, true)),
                                ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{})));

    FolderTreeService service(mockClient);
    Captured captured;

    service.loadSubfolders(0, true, captureInto(captured));

    ASSERT_TRUE(captured.called);
    EXPECT_TRUE(captured.result.success);
}

TEST(FolderTreeServiceTest, FiltersOutFilesKeepingOnlyFolders)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> mixed{
        {"file.txt", 1, 10, false, 0},
        {"folderA", 2, 0, true, 0},
        {"file2.txt", 3, 20, false, 0},
        {"folderB", 4, 0, true, 0},
    };

    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(mixed)));

    FolderTreeService service(mockClient);
    Captured captured;

    service.loadSubfolders(0, true, captureInto(captured));

    ASSERT_TRUE(captured.called);
    EXPECT_TRUE(captured.result.success);
    ASSERT_EQ(captured.result.value.size(), 2u);
    EXPECT_EQ(captured.result.value[0].handle, 2u);
    EXPECT_EQ(captured.result.value[1].handle, 4u);
}

TEST(FolderTreeServiceTest, FailurePropagates)
{
    auto mockClient = std::make_shared<MockMegaClient>();

    EXPECT_CALL(*mockClient, getChildren(7, ::testing::_, ::testing::_))
        .WillOnce(
            ::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::fail("network error", 3)));

    FolderTreeService service(mockClient);
    Captured captured;

    service.loadSubfolders(7, false, captureInto(captured));

    ASSERT_TRUE(captured.called);
    EXPECT_FALSE(captured.result.success);
    EXPECT_EQ(captured.result.errorCode, 3);
}
