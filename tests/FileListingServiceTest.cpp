#include "core/FileListingService.h"

#include "core/FolderNavigationService.h"
#include "MockMegaClient.h"
#include "MockNodeCache.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace
{

struct Captured
{
    bool cacheHitCalled = false;
    std::vector<FileEntry> cacheHitEntries;
    bool refreshedCalled = false;
    Result<std::vector<FileEntry>> refreshedResult;
};

std::function<void(std::vector<FileEntry>)> onCacheHitInto(Captured& captured)
{
    return [&captured](std::vector<FileEntry> entries) {
        captured.cacheHitCalled = true;
        captured.cacheHitEntries = std::move(entries);
    };
}

std::function<void(Result<std::vector<FileEntry>>)> onRefreshedInto(Captured& captured)
{
    return [&captured](Result<std::vector<FileEntry>> result) {
        captured.refreshedCalled = true;
        captured.refreshedResult = std::move(result);
    };
}

} // namespace

TEST(FileListingServiceTest, SuccessChainReturnsChildren)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockCache = std::make_shared<::testing::NiceMock<MockNodeCache>>();
    ON_CALL(*mockCache, loadChildren(::testing::_))
        .WillByDefault(::testing::Return(Result<std::vector<FileEntry>>::fail("no cache")));
    ON_CALL(*mockCache, saveChildren(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Result<void>::ok()));
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

    auto navigationService = std::make_shared<FolderNavigationService>(mockClient, mockCache);
    FileListingService service(mockClient, navigationService);
    Captured captured;

    // Act
    service.loadRootListing(
        "user@example.com", "pw", SortOrder{}, onCacheHitInto(captured), onRefreshedInto(captured));

    // Assert
    ASSERT_TRUE(captured.refreshedCalled);
    EXPECT_TRUE(captured.refreshedResult.success);
    EXPECT_EQ(captured.refreshedResult.value.size(), expected.size());
    EXPECT_EQ(captured.refreshedResult.value[0].name, "a.txt");
}

TEST(FileListingServiceTest, LoginFailureShortCircuitsWithoutTouchingCache)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockCache = std::make_shared<::testing::NiceMock<MockNodeCache>>();

    EXPECT_CALL(*mockClient, login(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<void>::fail("bad credentials", 1)));
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_)).Times(0);
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_)).Times(0);
    // Login failure short-circuits before FolderNavigationService::openRoot
    // is ever reached, so the cache is never consulted.
    EXPECT_CALL(*mockCache, loadChildren(::testing::_)).Times(0);

    auto navigationService = std::make_shared<FolderNavigationService>(mockClient, mockCache);
    FileListingService service(mockClient, navigationService);
    Captured captured;

    // Act
    service.loadRootListing("user@example.com",
                            "wrong",
                            SortOrder{},
                            onCacheHitInto(captured),
                            onRefreshedInto(captured));

    // Assert
    EXPECT_FALSE(captured.cacheHitCalled);
    ASSERT_TRUE(captured.refreshedCalled);
    EXPECT_FALSE(captured.refreshedResult.success);
    EXPECT_EQ(captured.refreshedResult.errorMessage, "bad credentials");
    EXPECT_EQ(captured.refreshedResult.errorCode, 1);
}

TEST(FileListingServiceTest, FetchNodesFailureShortCircuitsWithoutTouchingCache)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockCache = std::make_shared<::testing::NiceMock<MockNodeCache>>();

    EXPECT_CALL(*mockClient, login(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(Result<void>::fail("network error", 2)));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_)).Times(0);
    EXPECT_CALL(*mockCache, loadChildren(::testing::_)).Times(0);

    auto navigationService = std::make_shared<FolderNavigationService>(mockClient, mockCache);
    FileListingService service(mockClient, navigationService);
    Captured captured;

    // Act
    service.loadRootListing(
        "user@example.com", "pw", SortOrder{}, onCacheHitInto(captured), onRefreshedInto(captured));

    // Assert
    EXPECT_FALSE(captured.cacheHitCalled);
    ASSERT_TRUE(captured.refreshedCalled);
    EXPECT_FALSE(captured.refreshedResult.success);
    EXPECT_EQ(captured.refreshedResult.errorMessage, "network error");
    EXPECT_EQ(captured.refreshedResult.errorCode, 2);
}

TEST(FileListingServiceTest, CachedRootListingFiresOnCacheHitBeforeAuthoritativeResult)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockCache = std::make_shared<::testing::NiceMock<MockNodeCache>>();
    const std::vector<FileEntry> cachedEntries{{"cached-root.txt", 1, 10, false, 0}};
    const std::vector<FileEntry> freshEntries{{"fresh-root.txt", 2, 20, false, 0}};

    EXPECT_CALL(*mockClient, login(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(freshEntries)));
    EXPECT_CALL(*mockCache, loadChildren(::testing::_))
        .WillOnce(::testing::Return(Result<std::vector<FileEntry>>::ok(cachedEntries)));
    ON_CALL(*mockCache, saveChildren(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Result<void>::ok()));

    auto navigationService = std::make_shared<FolderNavigationService>(mockClient, mockCache);
    FileListingService service(mockClient, navigationService);
    Captured captured;

    // Act
    service.loadRootListing(
        "user@example.com", "pw", SortOrder{}, onCacheHitInto(captured), onRefreshedInto(captured));

    // Assert
    ASSERT_TRUE(captured.cacheHitCalled);
    EXPECT_EQ(captured.cacheHitEntries.size(), cachedEntries.size());
    ASSERT_TRUE(captured.refreshedCalled);
    EXPECT_TRUE(captured.refreshedResult.success);
    EXPECT_EQ(captured.refreshedResult.value.size(), freshEntries.size());
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
