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

::testing::Matcher<const INodeCache::ParentKey&> parentKeyIs(bool isRoot, std::uint64_t handle)
{
    return ::testing::AllOf(::testing::Field(&INodeCache::ParentKey::isRoot, isRoot),
                            ::testing::Field(&INodeCache::ParentKey::handle, handle));
}

// Default behavior for tests that aren't specifically about caching:
// every loadChildren call misses, every saveChildren call succeeds. Used
// with a NiceMock so incidental calls don't trigger "uninteresting call"
// warnings.
void installDefaultCacheBehavior(MockNodeCache& cache)
{
    ON_CALL(cache, loadChildren(::testing::_))
        .WillByDefault(::testing::Return(Result<std::vector<FileEntry>>::fail("no cache")));
    ON_CALL(cache, saveChildren(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Result<void>::ok()));
}

} // namespace

TEST(FolderNavigationServiceTest, OpenFolderSuccessUpdatesCurrentAndEnablesGoBack)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockCache = std::make_shared<::testing::NiceMock<MockNodeCache>>();
    installDefaultCacheBehavior(*mockCache);
    const std::vector<FileEntry> expected{{"nested.txt", 10, 50, false, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(expected)));

    FolderNavigationService service(mockClient, mockCache);
    Captured captured;

    // Act
    service.openFolder(1, SortOrder{}, onCacheHitInto(captured), onRefreshedInto(captured));

    // Assert
    ASSERT_TRUE(captured.refreshedCalled);
    EXPECT_TRUE(captured.refreshedResult.success);
    EXPECT_EQ(captured.refreshedResult.value.size(), expected.size());
    EXPECT_TRUE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, OpenFolderFailureLeavesCanGoBackFalse)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockCache = std::make_shared<::testing::NiceMock<MockNodeCache>>();
    installDefaultCacheBehavior(*mockCache);

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<std::vector<FileEntry>>::fail("invalid handle", 3)));

    FolderNavigationService service(mockClient, mockCache);
    Captured captured;

    // Act
    service.openFolder(1, SortOrder{}, onCacheHitInto(captured), onRefreshedInto(captured));

    // Assert
    ASSERT_TRUE(captured.refreshedCalled);
    EXPECT_FALSE(captured.refreshedResult.success);
    EXPECT_FALSE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, GoBackFromFolderReturnsToRootViaGetRootChildren)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockCache = std::make_shared<::testing::NiceMock<MockNodeCache>>();
    installDefaultCacheBehavior(*mockCache);
    const std::vector<FileEntry> folderChildren{{"a.txt", 2, 1, false, 0}};
    const std::vector<FileEntry> rootChildren{{"folder", 1, 0, true, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(folderChildren)));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(rootChildren)));

    FolderNavigationService service(mockClient, mockCache);
    Captured openCaptured;
    service.openFolder(1, SortOrder{}, onCacheHitInto(openCaptured), onRefreshedInto(openCaptured));
    ASSERT_TRUE(openCaptured.refreshedResult.success);

    // Act
    Captured backCaptured;
    service.goBack(SortOrder{}, onCacheHitInto(backCaptured), onRefreshedInto(backCaptured));

    // Assert
    ASSERT_TRUE(backCaptured.refreshedCalled);
    EXPECT_TRUE(backCaptured.refreshedResult.success);
    EXPECT_EQ(backCaptured.refreshedResult.value.size(), rootChildren.size());
    EXPECT_FALSE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, GoBackBetweenTwoNestedFoldersUsesGetChildren)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockCache = std::make_shared<::testing::NiceMock<MockNodeCache>>();
    installDefaultCacheBehavior(*mockCache);
    const std::vector<FileEntry> h1Children{{"sub", 2, 0, true, 0}};
    const std::vector<FileEntry> h2Children{{"b.txt", 3, 1, false, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .Times(2)
        .WillRepeatedly(
            ::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(h1Children)));
    EXPECT_CALL(*mockClient, getChildren(2, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(h2Children)));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_)).Times(0);

    FolderNavigationService service(mockClient, mockCache);
    Captured c1, c2, c3;
    service.openFolder(1, SortOrder{}, onCacheHitInto(c1), onRefreshedInto(c1));
    ASSERT_TRUE(c1.refreshedResult.success);
    service.openFolder(2, SortOrder{}, onCacheHitInto(c2), onRefreshedInto(c2));
    ASSERT_TRUE(c2.refreshedResult.success);

    // Act
    service.goBack(SortOrder{}, onCacheHitInto(c3), onRefreshedInto(c3));

    // Assert
    ASSERT_TRUE(c3.refreshedCalled);
    EXPECT_TRUE(c3.refreshedResult.success);
    EXPECT_TRUE(service.canGoBack()); // root is still one entry back
}

TEST(FolderNavigationServiceTest, GoBackFailureLeavesStackAndCurrentUnchanged)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockCache = std::make_shared<::testing::NiceMock<MockNodeCache>>();
    installDefaultCacheBehavior(*mockCache);
    const std::vector<FileEntry> h1Children{{"sub", 2, 0, true, 0}};
    const std::vector<FileEntry> rootChildren{{"folder", 1, 0, true, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(h1Children)));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(
            ::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::fail("network error", 2)))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(rootChildren)));

    FolderNavigationService service(mockClient, mockCache);
    Captured openCaptured;
    service.openFolder(1, SortOrder{}, onCacheHitInto(openCaptured), onRefreshedInto(openCaptured));
    ASSERT_TRUE(openCaptured.refreshedResult.success);

    // Act: first goBack fails
    Captured firstBack;
    service.goBack(SortOrder{}, onCacheHitInto(firstBack), onRefreshedInto(firstBack));

    // Assert: failure surfaced, stack/current untouched
    ASSERT_TRUE(firstBack.refreshedCalled);
    EXPECT_FALSE(firstBack.refreshedResult.success);
    EXPECT_TRUE(service.canGoBack());

    // Act: second goBack succeeds against the same (unconsumed) peek target
    Captured secondBack;
    service.goBack(SortOrder{}, onCacheHitInto(secondBack), onRefreshedInto(secondBack));

    // Assert
    ASSERT_TRUE(secondBack.refreshedCalled);
    EXPECT_TRUE(secondBack.refreshedResult.success);
    EXPECT_FALSE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, GoBackWithEmptyStackFails)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockCache = std::make_shared<::testing::NiceMock<MockNodeCache>>();
    installDefaultCacheBehavior(*mockCache);
    EXPECT_CALL(*mockClient, getChildren(::testing::_, ::testing::_, ::testing::_)).Times(0);
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_)).Times(0);
    // Empty back-stack fails before ever consulting the cache.
    EXPECT_CALL(*mockCache, loadChildren(::testing::_)).Times(0);

    FolderNavigationService service(mockClient, mockCache);
    Captured captured;

    // Act
    service.goBack(SortOrder{}, onCacheHitInto(captured), onRefreshedInto(captured));

    // Assert
    ASSERT_TRUE(captured.refreshedCalled);
    EXPECT_FALSE(captured.refreshedResult.success);
    EXPECT_FALSE(captured.cacheHitCalled);
}

TEST(FolderNavigationServiceTest, RefreshCurrentAtRootUsesGetRootChildrenWithoutTouchingStack)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockCache = std::make_shared<::testing::NiceMock<MockNodeCache>>();
    installDefaultCacheBehavior(*mockCache);
    const std::vector<FileEntry> rootChildren{{"folder", 1, 0, true, 0}};

    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(rootChildren)));
    EXPECT_CALL(*mockClient, getChildren(::testing::_, ::testing::_, ::testing::_)).Times(0);

    FolderNavigationService service(mockClient, mockCache);
    bool called = false;
    Result<std::vector<FileEntry>> captured;

    // Act: refresh while still at the initial root location
    service.refreshCurrent(SortOrder{}, [&](Result<std::vector<FileEntry>> result) {
        called = true;
        captured = std::move(result);
    });

    // Assert
    ASSERT_TRUE(called);
    EXPECT_TRUE(captured.success);
    EXPECT_FALSE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, RefreshCurrentInOpenedFolderUsesGetChildrenAndPreservesBackStack)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockCache = std::make_shared<::testing::NiceMock<MockNodeCache>>();
    installDefaultCacheBehavior(*mockCache);
    const std::vector<FileEntry> h1Children{{"a.txt", 2, 1, false, 0}};
    const std::vector<FileEntry> refreshed{{"a.txt", 2, 1, false, 0}, {"b.txt", 3, 2, false, 0}};
    const std::vector<FileEntry> rootChildren{{"folder", 1, 0, true, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(h1Children)))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(refreshed)));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(rootChildren)));

    FolderNavigationService service(mockClient, mockCache);
    Captured openCaptured;
    service.openFolder(1, SortOrder{}, onCacheHitInto(openCaptured), onRefreshedInto(openCaptured));
    ASSERT_TRUE(openCaptured.refreshedResult.success);
    ASSERT_TRUE(service.canGoBack());

    // Act: refresh the current folder (handle 1) with a different order
    bool refreshCalled = false;
    Result<std::vector<FileEntry>> refreshResult;
    service.refreshCurrent(SortOrder{SortKey::Size, false},
                           [&](Result<std::vector<FileEntry>> result) {
                               refreshCalled = true;
                               refreshResult = std::move(result);
                           });

    // Assert: refresh succeeded via getChildren, back-stack untouched
    ASSERT_TRUE(refreshCalled);
    EXPECT_TRUE(refreshResult.success);
    EXPECT_EQ(refreshResult.value.size(), refreshed.size());
    EXPECT_TRUE(service.canGoBack());

    // Further assert mCurrent is still handle 1 (not root, not popped): a
    // subsequent goBack must still resolve to the single root back-stack
    // entry via getRootChildren.
    Captured backCaptured;
    service.goBack(SortOrder{}, onCacheHitInto(backCaptured), onRefreshedInto(backCaptured));
    ASSERT_TRUE(backCaptured.refreshedCalled);
    EXPECT_TRUE(backCaptured.refreshedResult.success);
    EXPECT_FALSE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, OpenFolderCacheHitFiresBeforeNetworkResolves)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockCache = std::make_shared<::testing::NiceMock<MockNodeCache>>();
    const std::vector<FileEntry> cachedEntries{{"cached.txt", 5, 10, false, 0}};
    const std::vector<FileEntry> freshEntries{{"fresh.txt", 6, 20, false, 0}};

    EXPECT_CALL(*mockCache, loadChildren(parentKeyIs(false, 1)))
        .WillOnce(::testing::Return(Result<std::vector<FileEntry>>::ok(cachedEntries)));
    ON_CALL(*mockCache, saveChildren(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Result<void>::ok()));
    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(freshEntries)));

    FolderNavigationService service(mockClient, mockCache);
    std::vector<std::string> order;

    // Act
    service.openFolder(
        1,
        SortOrder{},
        [&order](std::vector<FileEntry>) {
            order.push_back("cacheHit");
        },
        [&order](Result<std::vector<FileEntry>>) {
            order.push_back("refreshed");
        });

    // Assert
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], "cacheHit");
    EXPECT_EQ(order[1], "refreshed");
}

TEST(FolderNavigationServiceTest, OpenFolderCacheMissNeverFiresOnCacheHit)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockCache = std::make_shared<::testing::NiceMock<MockNodeCache>>();
    installDefaultCacheBehavior(*mockCache); // loadChildren always misses
    const std::vector<FileEntry> freshEntries{{"fresh.txt", 6, 20, false, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(freshEntries)));

    FolderNavigationService service(mockClient, mockCache);
    Captured captured;

    // Act
    service.openFolder(1, SortOrder{}, onCacheHitInto(captured), onRefreshedInto(captured));

    // Assert
    EXPECT_FALSE(captured.cacheHitCalled);
    ASSERT_TRUE(captured.refreshedCalled);
    EXPECT_TRUE(captured.refreshedResult.success);
}

TEST(FolderNavigationServiceTest, OpenFolderEmptyCacheNeverFiresOnCacheHit)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockCache = std::make_shared<::testing::NiceMock<MockNodeCache>>();
    ON_CALL(*mockCache, loadChildren(::testing::_))
        .WillByDefault(::testing::Return(Result<std::vector<FileEntry>>::ok({}))); // hit, but empty
    ON_CALL(*mockCache, saveChildren(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(Result<void>::ok()));
    const std::vector<FileEntry> freshEntries{{"fresh.txt", 6, 20, false, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(freshEntries)));

    FolderNavigationService service(mockClient, mockCache);
    Captured captured;

    // Act
    service.openFolder(1, SortOrder{}, onCacheHitInto(captured), onRefreshedInto(captured));

    // Assert: a technically-successful-but-empty cache read is treated the
    // same as a miss -- per INodeCache.h/FolderNavigationService's shared
    // "empty means nothing to show yet" policy.
    EXPECT_FALSE(captured.cacheHitCalled);
    ASSERT_TRUE(captured.refreshedCalled);
}

TEST(FolderNavigationServiceTest, SuccessfulOpenFolderRefreshWritesThroughToCache)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockCache = std::make_shared<::testing::NiceMock<MockNodeCache>>();
    ON_CALL(*mockCache, loadChildren(::testing::_))
        .WillByDefault(::testing::Return(Result<std::vector<FileEntry>>::fail("no cache")));
    const std::vector<FileEntry> freshEntries{{"fresh.txt", 6, 20, false, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(freshEntries)));
    EXPECT_CALL(*mockCache, saveChildren(parentKeyIs(false, 1), freshEntries))
        .WillOnce(::testing::Return(Result<void>::ok()));

    FolderNavigationService service(mockClient, mockCache);
    Captured captured;

    // Act
    service.openFolder(1, SortOrder{}, onCacheHitInto(captured), onRefreshedInto(captured));

    // Assert (the EXPECT_CALL on saveChildren above is itself the assertion)
    ASSERT_TRUE(captured.refreshedCalled);
    EXPECT_TRUE(captured.refreshedResult.success);
}

TEST(FolderNavigationServiceTest, GoBackCacheHitUsesTargetLocationKeyNotCurrentLocation)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockCache = std::make_shared<::testing::NiceMock<MockNodeCache>>();
    installDefaultCacheBehavior(*mockCache);
    const std::vector<FileEntry> h1Children{{"a.txt", 2, 1, false, 0}};
    const std::vector<FileEntry> rootChildren{{"folder", 1, 0, true, 0}};
    const std::vector<FileEntry> cachedRootChildren{{"cached-folder", 1, 0, true, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(h1Children)));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(rootChildren)));
    // Once any EXPECT_CALL exists for loadChildren, every call must match
    // one of them (installDefaultCacheBehavior's ON_CALL alone no longer
    // suffices as a catch-all) -- register the general fallback first, most
    // specific last, so gmock's most-recently-set-wins rule picks the
    // specific one for goBack's target key and leaves this one to catch
    // openFolder's call against handle 1.
    EXPECT_CALL(*mockCache, loadChildren(::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Return(Result<std::vector<FileEntry>>::fail("no cache")));
    // goBack's target is root (isRoot=true, handle irrelevant) -- not
    // handle=1, which is where the service currently is before goBack.
    EXPECT_CALL(*mockCache, loadChildren(parentKeyIs(true, 0)))
        .WillOnce(::testing::Return(Result<std::vector<FileEntry>>::ok(cachedRootChildren)));

    FolderNavigationService service(mockClient, mockCache);
    Captured openCaptured;
    service.openFolder(1, SortOrder{}, onCacheHitInto(openCaptured), onRefreshedInto(openCaptured));
    ASSERT_TRUE(openCaptured.refreshedResult.success);

    // Act
    Captured backCaptured;
    service.goBack(SortOrder{}, onCacheHitInto(backCaptured), onRefreshedInto(backCaptured));

    // Assert
    ASSERT_TRUE(backCaptured.cacheHitCalled);
    EXPECT_EQ(backCaptured.cacheHitEntries.size(), cachedRootChildren.size());
}

TEST(FolderNavigationServiceTest, OpenRootCacheHitAndWriteThroughDoNotTouchBackStack)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockCache = std::make_shared<::testing::NiceMock<MockNodeCache>>();
    const std::vector<FileEntry> cachedEntries{{"cached-root.txt", 1, 10, false, 0}};
    const std::vector<FileEntry> freshEntries{{"fresh-root.txt", 2, 20, false, 0}};

    EXPECT_CALL(*mockCache, loadChildren(parentKeyIs(true, 0)))
        .WillOnce(::testing::Return(Result<std::vector<FileEntry>>::ok(cachedEntries)));
    EXPECT_CALL(*mockCache, saveChildren(parentKeyIs(true, 0), freshEntries))
        .WillOnce(::testing::Return(Result<void>::ok()));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(freshEntries)));

    FolderNavigationService service(mockClient, mockCache);
    Captured captured;

    // Act
    service.openRoot(SortOrder{}, onCacheHitInto(captured), onRefreshedInto(captured));

    // Assert
    ASSERT_TRUE(captured.cacheHitCalled);
    EXPECT_EQ(captured.cacheHitEntries.size(), cachedEntries.size());
    ASSERT_TRUE(captured.refreshedCalled);
    EXPECT_TRUE(captured.refreshedResult.success);
    EXPECT_FALSE(service.canGoBack()); // root is never pushed onto the back-stack
}

TEST(FolderNavigationServiceTest, ResetToRootClearsBackStackAndReturnsToRootLocation)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockCache = std::make_shared<::testing::NiceMock<MockNodeCache>>();
    installDefaultCacheBehavior(*mockCache);
    const std::vector<FileEntry> h1Children{{"sub", 2, 0, true, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(h1Children)));

    FolderNavigationService service(mockClient, mockCache);
    Captured openCaptured;
    service.openFolder(1, SortOrder{}, onCacheHitInto(openCaptured), onRefreshedInto(openCaptured));
    ASSERT_TRUE(openCaptured.refreshedResult.success);
    ASSERT_TRUE(service.canGoBack());

    // Act
    service.resetToRoot();

    // Assert
    EXPECT_FALSE(service.canGoBack());
    FolderNavigationService::CurrentLocation location = service.currentLocation();
    EXPECT_TRUE(location.isRoot);
}
