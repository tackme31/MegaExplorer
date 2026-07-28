#include "core/FileListingService.h"

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

} // namespace

TEST(FileListingServiceTest, SuccessChainReturnsChildren)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> expected{
        {"a.txt", 1, 100, false, 0},
        {"folder", 2, 0, true, 0},
    };

    EXPECT_CALL(*mockClient, login("user@example.com", "pw", ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(expected)));

    FileListingService service(mockClient);
    Captured captured;

    // Act
    service.loadRootListing(
        "user@example.com", "pw", SortOrder{}, [&captured](Result<std::vector<FileEntry>> result) {
            captured.called = true;
            captured.result = std::move(result);
        });

    // Assert
    ASSERT_TRUE(captured.called);
    EXPECT_TRUE(captured.result.success);
    EXPECT_EQ(captured.result.value.size(), expected.size());
    EXPECT_EQ(captured.result.value[0].name, "a.txt");
}

TEST(FileListingServiceTest, LoginFailureShortCircuits)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();

    EXPECT_CALL(*mockClient, login(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<void>::fail("bad credentials", 1)));
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_)).Times(0);
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_)).Times(0);

    FileListingService service(mockClient);
    Captured captured;

    // Act
    service.loadRootListing("user@example.com",
                            "wrong",
                            SortOrder{},
                            [&captured](Result<std::vector<FileEntry>> result) {
                                captured.called = true;
                                captured.result = std::move(result);
                            });

    // Assert
    ASSERT_TRUE(captured.called);
    EXPECT_FALSE(captured.result.success);
    EXPECT_EQ(captured.result.errorMessage, "bad credentials");
    EXPECT_EQ(captured.result.errorCode, 1);
}

TEST(FileListingServiceTest, FetchNodesFailureShortCircuits)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();

    EXPECT_CALL(*mockClient, login(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(Result<void>::fail("network error", 2)));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_)).Times(0);

    FileListingService service(mockClient);
    Captured captured;

    // Act
    service.loadRootListing(
        "user@example.com", "pw", SortOrder{}, [&captured](Result<std::vector<FileEntry>> result) {
            captured.called = true;
            captured.result = std::move(result);
        });

    // Assert
    ASSERT_TRUE(captured.called);
    EXPECT_FALSE(captured.result.success);
    EXPECT_EQ(captured.result.errorMessage, "network error");
    EXPECT_EQ(captured.result.errorCode, 2);
}

TEST(FileListingServiceTest, MockGetChildrenForwardsResultToCallback)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> expected{{"nested.txt", 3, 50, false, 0}};

    EXPECT_CALL(*mockClient, getChildren(42, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(expected)));

    Result<std::vector<FileEntry>> captured;

    // Act
    mockClient->getChildren(42, SortOrder{}, [&captured](Result<std::vector<FileEntry>> result) {
        captured = std::move(result);
    });

    // Assert
    EXPECT_TRUE(captured.success);
    ASSERT_EQ(captured.value.size(), 1u);
    EXPECT_EQ(captured.value[0].name, "nested.txt");
}
