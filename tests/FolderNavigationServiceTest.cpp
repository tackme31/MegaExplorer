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

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(expected)));

    FolderNavigationService service(mockClient);
    Captured captured;

    // Act
    service.openFolder(1, captureInto(captured));

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

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(
            Result<std::vector<FileEntry>>::fail("invalid handle", 3)));

    FolderNavigationService service(mockClient);
    Captured captured;

    // Act
    service.openFolder(1, captureInto(captured));

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

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(folderChildren)));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(Result<std::vector<FileEntry>>::ok(rootChildren)));

    FolderNavigationService service(mockClient);
    Captured openCaptured;
    service.openFolder(1, captureInto(openCaptured));
    ASSERT_TRUE(openCaptured.result.success);

    // Act
    Captured backCaptured;
    service.goBack(captureInto(backCaptured));

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

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_))
        .Times(2)
        .WillRepeatedly(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(h1Children)));
    EXPECT_CALL(*mockClient, getChildren(2, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(h2Children)));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_)).Times(0);

    FolderNavigationService service(mockClient);
    Captured c1, c2, c3;
    service.openFolder(1, captureInto(c1));
    ASSERT_TRUE(c1.result.success);
    service.openFolder(2, captureInto(c2));
    ASSERT_TRUE(c2.result.success);

    // Act
    service.goBack(captureInto(c3));

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

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(h1Children)));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(
            Result<std::vector<FileEntry>>::fail("network error", 2)))
        .WillOnce(::testing::InvokeArgument<0>(Result<std::vector<FileEntry>>::ok(rootChildren)));

    FolderNavigationService service(mockClient);
    Captured openCaptured;
    service.openFolder(1, captureInto(openCaptured));
    ASSERT_TRUE(openCaptured.result.success);

    // Act: first goBack fails
    Captured firstBack;
    service.goBack(captureInto(firstBack));

    // Assert: failure surfaced, stack/current untouched
    ASSERT_TRUE(firstBack.called);
    EXPECT_FALSE(firstBack.result.success);
    EXPECT_TRUE(service.canGoBack());

    // Act: second goBack succeeds against the same (unconsumed) peek target
    Captured secondBack;
    service.goBack(captureInto(secondBack));

    // Assert
    ASSERT_TRUE(secondBack.called);
    EXPECT_TRUE(secondBack.result.success);
    EXPECT_FALSE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, GoBackWithEmptyStackFails)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getChildren(::testing::_, ::testing::_)).Times(0);
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_)).Times(0);

    FolderNavigationService service(mockClient);
    Captured captured;

    // Act
    service.goBack(captureInto(captured));

    // Assert
    ASSERT_TRUE(captured.called);
    EXPECT_FALSE(captured.result.success);
}
