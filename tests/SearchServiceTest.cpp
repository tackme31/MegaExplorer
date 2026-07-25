#include "core/SearchService.h"
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

TEST(SearchServiceTest, SearchAtRootPassesRootSentinelToClient)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> expected{{"found.txt", 5, 10, false, 0}};

    EXPECT_CALL(*mockClient, search(::testing::_, true, std::string("query"), ::testing::_))
        .WillOnce(::testing::InvokeArgument<3>(Result<std::vector<FileEntry>>::ok(expected)));

    auto navigationService = std::make_shared<FolderNavigationService>(mockClient);
    SearchService service(mockClient, navigationService);
    Captured captured;

    // Act: no openFolder called, so navigationService is still at root
    service.search("query", captureInto(captured));

    // Assert
    ASSERT_TRUE(captured.called);
    EXPECT_TRUE(captured.result.success);
    EXPECT_EQ(captured.result.value.size(), expected.size());
}

TEST(SearchServiceTest, SearchInOpenedFolderPassesItsHandleToClient)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> folderChildren{{"sub", 2, 0, true, 0}};
    const std::vector<FileEntry> expected{{"found.txt", 5, 10, false, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(folderChildren)));
    EXPECT_CALL(*mockClient, search(1, false, std::string("query"), ::testing::_))
        .WillOnce(::testing::InvokeArgument<3>(Result<std::vector<FileEntry>>::ok(expected)));

    auto navigationService = std::make_shared<FolderNavigationService>(mockClient);
    Captured openCaptured;
    navigationService->openFolder(1, captureInto(openCaptured));
    ASSERT_TRUE(openCaptured.result.success);

    SearchService service(mockClient, navigationService);
    Captured captured;

    // Act
    service.search("query", captureInto(captured));

    // Assert
    ASSERT_TRUE(captured.called);
    EXPECT_TRUE(captured.result.success);
    EXPECT_EQ(captured.result.value.size(), expected.size());
}

TEST(SearchServiceTest, SearchWithNoMatchesSucceedsWithEmptyResult)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();

    EXPECT_CALL(*mockClient, search(::testing::_, true, std::string("nomatch"), ::testing::_))
        .WillOnce(::testing::InvokeArgument<3>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{})));

    auto navigationService = std::make_shared<FolderNavigationService>(mockClient);
    SearchService service(mockClient, navigationService);
    Captured captured;

    // Act
    service.search("nomatch", captureInto(captured));

    // Assert: empty results is still a success, not a failure
    ASSERT_TRUE(captured.called);
    EXPECT_TRUE(captured.result.success);
    EXPECT_TRUE(captured.result.value.empty());
}

TEST(SearchServiceTest, SearchFailurePropagates)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();

    EXPECT_CALL(*mockClient, search(::testing::_, true, std::string("query"), ::testing::_))
        .WillOnce(::testing::InvokeArgument<3>(
            Result<std::vector<FileEntry>>::fail("network error", 2)));

    auto navigationService = std::make_shared<FolderNavigationService>(mockClient);
    SearchService service(mockClient, navigationService);
    Captured captured;

    // Act
    service.search("query", captureInto(captured));

    // Assert
    ASSERT_TRUE(captured.called);
    EXPECT_FALSE(captured.result.success);
}

TEST(SearchServiceTest, EmptyQueryFailsWithoutCallingClient)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, search(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .Times(0);

    auto navigationService = std::make_shared<FolderNavigationService>(mockClient);
    SearchService service(mockClient, navigationService);
    Captured captured;

    // Act
    service.search("", captureInto(captured));

    // Assert
    ASSERT_TRUE(captured.called);
    EXPECT_FALSE(captured.result.success);
}
