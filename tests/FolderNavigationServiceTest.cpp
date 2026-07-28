#include "core/FolderNavigationService.h"

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

TEST(FolderNavigationServiceTest, OpenFolderSuccessUpdatesCurrentAndEnablesGoBack)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> expected{{"nested.txt", 10, 50, false, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(expected)));

    FolderNavigationService service(mockClient);
    Captured captured;

    // Act
    service.openFolder(1, SortOrder{}, captureInto(captured));

    // Assert
    ASSERT_TRUE(captured.called);
    EXPECT_TRUE(captured.result.success);
    EXPECT_EQ(captured.result.value.size(), expected.size());
    EXPECT_TRUE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, OpenFolderFailureLeavesCanGoBackFalse)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<std::vector<FileEntry>>::fail("invalid handle", 3)));

    FolderNavigationService service(mockClient);
    Captured captured;

    // Act
    service.openFolder(1, SortOrder{}, captureInto(captured));

    // Assert
    ASSERT_TRUE(captured.called);
    EXPECT_FALSE(captured.result.success);
    EXPECT_FALSE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, GoBackFromFolderReturnsToRootViaGetRootChildren)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> folderChildren{{"a.txt", 2, 1, false, 0}};
    const std::vector<FileEntry> rootChildren{{"folder", 1, 0, true, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(folderChildren)));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(rootChildren)));

    FolderNavigationService service(mockClient);
    Captured openCaptured;
    service.openFolder(1, SortOrder{}, captureInto(openCaptured));
    ASSERT_TRUE(openCaptured.result.success);

    // Act
    Captured backCaptured;
    service.goBack(SortOrder{}, captureInto(backCaptured));

    // Assert
    ASSERT_TRUE(backCaptured.called);
    EXPECT_TRUE(backCaptured.result.success);
    EXPECT_EQ(backCaptured.result.value.size(), rootChildren.size());
    EXPECT_FALSE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, GoBackBetweenTwoNestedFoldersUsesGetChildren)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> h1Children{{"sub", 2, 0, true, 0}};
    const std::vector<FileEntry> h2Children{{"b.txt", 3, 1, false, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .Times(2)
        .WillRepeatedly(
            ::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(h1Children)));
    EXPECT_CALL(*mockClient, getChildren(2, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(h2Children)));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_)).Times(0);

    FolderNavigationService service(mockClient);
    Captured c1, c2, c3;
    service.openFolder(1, SortOrder{}, captureInto(c1));
    ASSERT_TRUE(c1.result.success);
    service.openFolder(2, SortOrder{}, captureInto(c2));
    ASSERT_TRUE(c2.result.success);

    // Act
    service.goBack(SortOrder{}, captureInto(c3));

    // Assert
    ASSERT_TRUE(c3.called);
    EXPECT_TRUE(c3.result.success);
    EXPECT_TRUE(service.canGoBack()); // root is still one entry back
}

TEST(FolderNavigationServiceTest, GoBackFailureLeavesStackAndCurrentUnchanged)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> h1Children{{"sub", 2, 0, true, 0}};
    const std::vector<FileEntry> rootChildren{{"folder", 1, 0, true, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(h1Children)));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(
            ::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::fail("network error", 2)))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(rootChildren)));

    FolderNavigationService service(mockClient);
    Captured openCaptured;
    service.openFolder(1, SortOrder{}, captureInto(openCaptured));
    ASSERT_TRUE(openCaptured.result.success);

    // Act: first goBack fails
    Captured firstBack;
    service.goBack(SortOrder{}, captureInto(firstBack));

    // Assert: failure surfaced, stack/current untouched
    ASSERT_TRUE(firstBack.called);
    EXPECT_FALSE(firstBack.result.success);
    EXPECT_TRUE(service.canGoBack());

    // Act: second goBack succeeds against the same (unconsumed) peek target
    Captured secondBack;
    service.goBack(SortOrder{}, captureInto(secondBack));

    // Assert
    ASSERT_TRUE(secondBack.called);
    EXPECT_TRUE(secondBack.result.success);
    EXPECT_FALSE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, GoBackWithEmptyStackFails)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getChildren(::testing::_, ::testing::_, ::testing::_)).Times(0);
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_)).Times(0);

    FolderNavigationService service(mockClient);
    Captured captured;

    // Act
    service.goBack(SortOrder{}, captureInto(captured));

    // Assert
    ASSERT_TRUE(captured.called);
    EXPECT_FALSE(captured.result.success);
}

TEST(FolderNavigationServiceTest, RefreshCurrentAtRootUsesGetRootChildrenWithoutTouchingStack)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> rootChildren{{"folder", 1, 0, true, 0}};

    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(rootChildren)));
    EXPECT_CALL(*mockClient, getChildren(::testing::_, ::testing::_, ::testing::_)).Times(0);

    FolderNavigationService service(mockClient);
    Captured captured;

    // Act: refresh while still at the initial root location
    service.refreshCurrent(SortOrder{}, captureInto(captured));

    // Assert
    ASSERT_TRUE(captured.called);
    EXPECT_TRUE(captured.result.success);
    EXPECT_FALSE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, RefreshCurrentInOpenedFolderUsesGetChildrenAndPreservesBackStack)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> h1Children{{"a.txt", 2, 1, false, 0}};
    const std::vector<FileEntry> refreshed{{"a.txt", 2, 1, false, 0}, {"b.txt", 3, 2, false, 0}};
    const std::vector<FileEntry> rootChildren{{"folder", 1, 0, true, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(h1Children)))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(refreshed)));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(rootChildren)));

    FolderNavigationService service(mockClient);
    Captured openCaptured;
    service.openFolder(1, SortOrder{}, captureInto(openCaptured));
    ASSERT_TRUE(openCaptured.result.success);
    ASSERT_TRUE(service.canGoBack());

    // Act: refresh the current folder (handle 1) with a different order
    Captured refreshCaptured;
    service.refreshCurrent(SortOrder{SortKey::Size, false}, captureInto(refreshCaptured));

    // Assert: refresh succeeded via getChildren, back-stack untouched
    ASSERT_TRUE(refreshCaptured.called);
    EXPECT_TRUE(refreshCaptured.result.success);
    EXPECT_EQ(refreshCaptured.result.value.size(), refreshed.size());
    EXPECT_TRUE(service.canGoBack());

    // Further assert mCurrent is still handle 1 (not root, not popped): a
    // subsequent goBack must still resolve to the single root back-stack
    // entry via getRootChildren.
    Captured backCaptured;
    service.goBack(SortOrder{}, captureInto(backCaptured));
    ASSERT_TRUE(backCaptured.called);
    EXPECT_TRUE(backCaptured.result.success);
    EXPECT_FALSE(service.canGoBack());
}
