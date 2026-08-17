#include "core/FolderNavigationService.h"

#include "MockMegaClient.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace
{

struct Captured
{
    bool doneCalled = false;
    Result<std::vector<FileEntry>> doneResult;
};

std::function<void(Result<std::vector<FileEntry>>)> onDoneInto(Captured& captured)
{
    return [&captured](Result<std::vector<FileEntry>> result) {
        captured.doneCalled = true;
        captured.doneResult = std::move(result);
    };
}

struct CapturedPath
{
    bool doneCalled = false;
    Result<std::vector<PathSegment>> doneResult;
};

std::function<void(Result<std::vector<PathSegment>>)> onPathDoneInto(CapturedPath& captured)
{
    return [&captured](Result<std::vector<PathSegment>> result) {
        captured.doneCalled = true;
        captured.doneResult = std::move(result);
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
    service.openFolder(1, SortOrder{}, onDoneInto(captured));

    // Assert
    ASSERT_TRUE(captured.doneCalled);
    EXPECT_TRUE(captured.doneResult.success);
    EXPECT_EQ(captured.doneResult.value().size(), expected.size());
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
    service.openFolder(1, SortOrder{}, onDoneInto(captured));

    // Assert
    ASSERT_TRUE(captured.doneCalled);
    EXPECT_FALSE(captured.doneResult.success);
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
    service.openFolder(1, SortOrder{}, onDoneInto(openCaptured));
    ASSERT_TRUE(openCaptured.doneResult.success);

    // Act
    Captured backCaptured;
    service.goBack(SortOrder{}, onDoneInto(backCaptured));

    // Assert
    ASSERT_TRUE(backCaptured.doneCalled);
    EXPECT_TRUE(backCaptured.doneResult.success);
    EXPECT_EQ(backCaptured.doneResult.value().size(), rootChildren.size());
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
    service.openFolder(1, SortOrder{}, onDoneInto(c1));
    ASSERT_TRUE(c1.doneResult.success);
    service.openFolder(2, SortOrder{}, onDoneInto(c2));
    ASSERT_TRUE(c2.doneResult.success);

    // Act
    service.goBack(SortOrder{}, onDoneInto(c3));

    // Assert
    ASSERT_TRUE(c3.doneCalled);
    EXPECT_TRUE(c3.doneResult.success);
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
    service.openFolder(1, SortOrder{}, onDoneInto(openCaptured));
    ASSERT_TRUE(openCaptured.doneResult.success);

    // Act: first goBack fails
    Captured firstBack;
    service.goBack(SortOrder{}, onDoneInto(firstBack));

    // Assert: failure surfaced, stack/current untouched
    ASSERT_TRUE(firstBack.doneCalled);
    EXPECT_FALSE(firstBack.doneResult.success);
    EXPECT_TRUE(service.canGoBack());

    // Act: second goBack succeeds against the same (unconsumed) peek target
    Captured secondBack;
    service.goBack(SortOrder{}, onDoneInto(secondBack));

    // Assert
    ASSERT_TRUE(secondBack.doneCalled);
    EXPECT_TRUE(secondBack.doneResult.success);
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
    service.goBack(SortOrder{}, onDoneInto(captured));

    // Assert
    ASSERT_TRUE(captured.doneCalled);
    EXPECT_FALSE(captured.doneResult.success);
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
    service.openFolder(1, SortOrder{}, onDoneInto(openCaptured));
    ASSERT_TRUE(openCaptured.doneResult.success);
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
    EXPECT_EQ(refreshResult.value().size(), refreshed.size());
    EXPECT_TRUE(service.canGoBack());

    // Further assert mCurrent is still handle 1 (not root, not popped): a
    // subsequent goBack must still resolve to the single root back-stack
    // entry via getRootChildren.
    Captured backCaptured;
    service.goBack(SortOrder{}, onDoneInto(backCaptured));
    ASSERT_TRUE(backCaptured.doneCalled);
    EXPECT_TRUE(backCaptured.doneResult.success);
    EXPECT_FALSE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, ResetToRootClearsBackStackAndReturnsToRootLocation)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> h1Children{{"sub", 2, 0, true, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(h1Children)));

    FolderNavigationService service(mockClient);
    Captured openCaptured;
    service.openFolder(1, SortOrder{}, onDoneInto(openCaptured));
    ASSERT_TRUE(openCaptured.doneResult.success);
    ASSERT_TRUE(service.canGoBack());

    // Act
    service.resetToRoot();

    // Assert
    EXPECT_FALSE(service.canGoBack());
    FolderNavigationService::CurrentLocation location = service.currentLocation();
    EXPECT_TRUE(location.isRoot);
}

TEST(FolderNavigationServiceTest, NavigateToRootFromFolderUsesGetRootChildrenAndPushesHistory)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> h1Children{{"sub", 2, 0, true, 0}};
    const std::vector<FileEntry> rootChildren{{"folder", 1, 0, true, 0}};

    // getChildren(1, ...) is expected twice: once for the initial
    // openFolder(1), once more when goBack() below re-fetches the folder
    // navigateTo(0, true) pushed onto the back-stack.
    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .Times(2)
        .WillRepeatedly(
            ::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(h1Children)));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(rootChildren)));

    FolderNavigationService service(mockClient);
    Captured openCaptured;
    service.openFolder(1, SortOrder{}, onDoneInto(openCaptured));
    ASSERT_TRUE(openCaptured.doneResult.success);

    // Act
    Captured navigateCaptured;
    service.navigateTo(0, true, ViewKind::CloudDrive, SortOrder{}, onDoneInto(navigateCaptured));

    // Assert
    ASSERT_TRUE(navigateCaptured.doneCalled);
    EXPECT_TRUE(navigateCaptured.doneResult.success);
    EXPECT_TRUE(service.canGoBack());

    // A subsequent goBack must return to the folder navigateTo left behind,
    // proving it pushed history rather than truncating it.
    Captured backCaptured;
    service.goBack(SortOrder{}, onDoneInto(backCaptured));
    ASSERT_TRUE(backCaptured.doneCalled);
    EXPECT_TRUE(backCaptured.doneResult.success);
    EXPECT_EQ(backCaptured.doneResult.value().size(), h1Children.size());
    EXPECT_TRUE(service.canGoBack()); // root is still one entry back
}

TEST(FolderNavigationServiceTest, NavigateToFolderPushesHistoryLikeOpenFolder)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> expected{{"nested.txt", 10, 50, false, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(expected)));

    FolderNavigationService service(mockClient);
    Captured captured;

    // Act
    service.navigateTo(1, false, ViewKind::CloudDrive, SortOrder{}, onDoneInto(captured));

    // Assert
    ASSERT_TRUE(captured.doneCalled);
    EXPECT_TRUE(captured.doneResult.success);
    EXPECT_EQ(captured.doneResult.value().size(), expected.size());
    EXPECT_TRUE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, NavigateToFailureLeavesStateUnchanged)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<std::vector<FileEntry>>::fail("invalid handle", 3)));

    FolderNavigationService service(mockClient);
    Captured captured;

    // Act
    service.navigateTo(1, false, ViewKind::CloudDrive, SortOrder{}, onDoneInto(captured));

    // Assert: runAndCommit only commits on success, so a failed navigateTo
    // must leave canGoBack() (and, transitively, mCurrent) untouched.
    ASSERT_TRUE(captured.doneCalled);
    EXPECT_FALSE(captured.doneResult.success);
    EXPECT_FALSE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, ListChildrenOfReadsAnArbitraryFolderWithoutNavigating)
{
    // Arrange: the service is sitting in folder 1, and something asks about
    // folder 7 (a drag-copy's drop target).
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> inSeven{{"there.txt", 70, 50, false, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok({})));
    EXPECT_CALL(*mockClient, getChildren(7, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(inSeven)));

    FolderNavigationService service(mockClient);
    Captured opened;
    service.openFolder(1, SortOrder{}, onDoneInto(opened));
    Captured captured;

    // Act
    service.listChildrenOf(7, false, SortOrder{}, onDoneInto(captured));

    // Assert: the answer is folder 7's, and nothing about where the service
    // stands has moved -- back-stack still holds only the root, current is
    // still folder 1 (which the next refreshCurrent would prove).
    ASSERT_TRUE(captured.doneCalled);
    ASSERT_EQ(captured.doneResult.value().size(), 1u);
    EXPECT_EQ(captured.doneResult.value()[0].name, "there.txt");
    EXPECT_TRUE(service.canGoBack());
    EXPECT_FALSE(service.currentLocation().isRoot);
    EXPECT_EQ(service.currentLocation().handle, 1u);
}

TEST(FolderNavigationServiceTest, ListChildrenOfHonoursTheRootSentinel)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok({})));
    EXPECT_CALL(*mockClient, getChildren(::testing::_, ::testing::_, ::testing::_)).Times(0);

    FolderNavigationService service(mockClient);
    Captured captured;

    // handle 99 is meaningless when isRoot is true, same convention as
    // navigateTo's.
    service.listChildrenOf(99, true, SortOrder{}, onDoneInto(captured));

    ASSERT_TRUE(captured.doneCalled);
    EXPECT_TRUE(captured.doneResult.success);
}

TEST(FolderNavigationServiceTest, ResolveCurrentPathAtRootQueriesRootSentinel)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<PathSegment> rootPath{{"", 0, true}};

    EXPECT_CALL(*mockClient, getPath(::testing::_, true, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<PathSegment>>::ok(rootPath)));

    FolderNavigationService service(mockClient);
    CapturedPath captured;

    // Act: freshly constructed service is still at the root sentinel.
    service.resolveCurrentPath(onPathDoneInto(captured));

    // Assert
    ASSERT_TRUE(captured.doneCalled);
    EXPECT_TRUE(captured.doneResult.success);
    ASSERT_EQ(captured.doneResult.value().size(), 1u);
    EXPECT_TRUE(captured.doneResult.value()[0].isRoot);
}

TEST(FolderNavigationServiceTest, ResolveCurrentPathAfterOpenFolderQueriesCurrentHandle)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> h1Children{{"sub", 2, 0, true, 0}};
    const std::vector<PathSegment> path{{"", 0, true}, {"folder", 1, false}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(h1Children)));
    EXPECT_CALL(*mockClient, getPath(1, false, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<PathSegment>>::ok(path)));

    FolderNavigationService service(mockClient);
    Captured openCaptured;
    service.openFolder(1, SortOrder{}, onDoneInto(openCaptured));
    ASSERT_TRUE(openCaptured.doneResult.success);

    // Act
    CapturedPath pathCaptured;
    service.resolveCurrentPath(onPathDoneInto(pathCaptured));

    // Assert
    ASSERT_TRUE(pathCaptured.doneCalled);
    EXPECT_TRUE(pathCaptured.doneResult.success);
    EXPECT_EQ(pathCaptured.doneResult.value(), path);
}

TEST(FolderNavigationServiceTest, ResolveCurrentPathAfterGoBackQueriesRestoredLocation)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> h1Children{{"sub", 2, 0, true, 0}};
    const std::vector<FileEntry> h2Children{{"b.txt", 3, 1, false, 0}};
    const std::vector<PathSegment> path{{"", 0, true}, {"folder", 1, false}};

    // getChildren(1, ...) is expected twice: once for the initial
    // openFolder(1), once more when goBack() below re-fetches the folder
    // opening handle 2 pushed onto the back-stack.
    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .Times(2)
        .WillRepeatedly(
            ::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(h1Children)));
    EXPECT_CALL(*mockClient, getChildren(2, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(h2Children)));
    EXPECT_CALL(*mockClient, getPath(1, false, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<PathSegment>>::ok(path)));

    FolderNavigationService service(mockClient);
    Captured c1, c2;
    service.openFolder(1, SortOrder{}, onDoneInto(c1));
    ASSERT_TRUE(c1.doneResult.success);
    service.openFolder(2, SortOrder{}, onDoneInto(c2));
    ASSERT_TRUE(c2.doneResult.success);

    Captured backCaptured;
    service.goBack(SortOrder{}, onDoneInto(backCaptured));
    ASSERT_TRUE(backCaptured.doneResult.success);

    // Act: current location is now back to handle 1, restored via goBack --
    // not the deeper handle 2 that a naive "last opened" tracker might use.
    CapturedPath pathCaptured;
    service.resolveCurrentPath(onPathDoneInto(pathCaptured));

    // Assert
    ASSERT_TRUE(pathCaptured.doneCalled);
    EXPECT_TRUE(pathCaptured.doneResult.success);
    EXPECT_EQ(pathCaptured.doneResult.value(), path);
}

// --- Favourites, a location that is not a folder -----------------------------

TEST(FolderNavigationServiceTest, OpenFavouritesPushesTheCurrentLocationAndSwitchesKind)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> favourites{{"kept.txt", 5, 1, false, 0, false, true}};

    EXPECT_CALL(*mockClient, listFavourites(::testing::_, "", ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(favourites)));

    FolderNavigationService service(mockClient);
    Captured captured;

    // Act
    service.openFavourites(SortOrder{}, onDoneInto(captured));

    // Assert
    ASSERT_TRUE(captured.doneCalled);
    EXPECT_TRUE(captured.doneResult.success);
    EXPECT_EQ(captured.doneResult.value().size(), favourites.size());
    EXPECT_EQ(service.currentLocation().kind, ViewKind::Favourites);
    EXPECT_TRUE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, OpenFavouritesFailureLeavesTheLocationUnchanged)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();

    EXPECT_CALL(*mockClient, listFavourites(::testing::_, "", ::testing::_))
        .WillOnce(
            ::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::fail("not logged in", 3)));

    FolderNavigationService service(mockClient);
    Captured captured;

    // Act
    service.openFavourites(SortOrder{}, onDoneInto(captured));

    // Assert: runAndCommit only commits on success, kind included
    ASSERT_TRUE(captured.doneCalled);
    EXPECT_FALSE(captured.doneResult.success);
    EXPECT_EQ(service.currentLocation().kind, ViewKind::CloudDrive);
    EXPECT_FALSE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, OpenFavouritesWhileAlreadyThereRefetchesWithoutPushing)
{
    // Arrange: the side-panel row stays clickable while its own screen is showing.
    // Without the guard the same location would stack up and Back would walk down
    // through repeats of it.
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> favourites{{"kept.txt", 5, 1, false, 0, false, true}};

    EXPECT_CALL(*mockClient, listFavourites(::testing::_, "", ::testing::_))
        .Times(2)
        .WillRepeatedly(
            ::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(favourites)));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{})));

    FolderNavigationService service(mockClient);
    Captured first, second;
    service.openFavourites(SortOrder{}, onDoneInto(first));
    ASSERT_TRUE(first.doneResult.success);

    // Act
    service.openFavourites(SortOrder{}, onDoneInto(second));

    // Assert: re-fetched (the second expected call), one back-stack entry either way
    ASSERT_TRUE(second.doneCalled);
    EXPECT_TRUE(second.doneResult.success);
    EXPECT_EQ(service.currentLocation().kind, ViewKind::Favourites);
    ASSERT_TRUE(service.canGoBack());

    // One Back is enough to leave: a second push would have stranded a repeat of
    // the same screen underneath this one.
    Captured back;
    service.goBack(SortOrder{}, onDoneInto(back));
    ASSERT_TRUE(back.doneResult.success);
    EXPECT_EQ(service.currentLocation().kind, ViewKind::CloudDrive);
    EXPECT_FALSE(service.canGoBack());
}

// --- Rubbish bin, a location that IS a folder tree ---------------------------

TEST(FolderNavigationServiceTest, OpenRubbishPushesTheCurrentLocationAndSwitchesKind)
{
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> binned{{"gone.txt", 5, 1, false, 0}};

    EXPECT_CALL(*mockClient, getRubbishChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(binned)));

    FolderNavigationService service(mockClient);
    Captured captured;

    service.openRubbish(SortOrder{}, onDoneInto(captured));

    ASSERT_TRUE(captured.doneCalled);
    EXPECT_TRUE(captured.doneResult.success);
    EXPECT_EQ(service.currentLocation().kind, ViewKind::Rubbish);
    // isRoot means the bin's own top here, which is what tells it from a folder
    // inside the bin.
    EXPECT_TRUE(service.currentLocation().isRoot);
    EXPECT_TRUE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, OpenRubbishWhileAtTheBinTopRefetchesWithoutPushing)
{
    auto mockClient = std::make_shared<MockMegaClient>();

    EXPECT_CALL(*mockClient, getRubbishChildren(::testing::_, ::testing::_))
        .Times(2)
        .WillRepeatedly(::testing::InvokeArgument<1>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{})));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{})));

    FolderNavigationService service(mockClient);
    Captured first, second;
    service.openRubbish(SortOrder{}, onDoneInto(first));
    ASSERT_TRUE(first.doneResult.success);

    service.openRubbish(SortOrder{}, onDoneInto(second));

    ASSERT_TRUE(second.doneResult.success);
    EXPECT_EQ(service.currentLocation().kind, ViewKind::Rubbish);

    // One Back leaves, so the second click pushed nothing.
    Captured back;
    service.goBack(SortOrder{}, onDoneInto(back));
    ASSERT_TRUE(back.doneResult.success);
    EXPECT_EQ(service.currentLocation().kind, ViewKind::CloudDrive);
    EXPECT_FALSE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, OpeningAFolderFromInsideTheBinStaysInTheBin)
{
    // What separates the bin from the favourites listing: its contents are real
    // nodes, so opening one must not silently return the tab to the Cloud Drive --
    // the kind is the only thing gating what may be done to a binned node.
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> binned{{"folder", 7, 0, true, 0}};

    EXPECT_CALL(*mockClient, getRubbishChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(binned)));
    EXPECT_CALL(*mockClient, getChildren(7, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{})));

    FolderNavigationService service(mockClient);
    Captured opened, entered;
    service.openRubbish(SortOrder{}, onDoneInto(opened));
    ASSERT_TRUE(opened.doneResult.success);

    service.openFolder(7, SortOrder{}, onDoneInto(entered));

    ASSERT_TRUE(entered.doneResult.success);
    EXPECT_EQ(service.currentLocation().kind, ViewKind::Rubbish);
    EXPECT_FALSE(service.currentLocation().isRoot);
    EXPECT_EQ(service.currentLocation().handle, 7u);
}

TEST(FolderNavigationServiceTest, NavigatingUpOutOfABinnedFolderLandsOnTheBinTop)
{
    // isRoot alone used to mean "the Cloud Drive root", so "up" from a folder
    // directly under the bin fetched the Drive instead -- silently leaving the
    // screen whose kind is the only thing withholding destructive actions.
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> binned{{"folder", 7, 0, true, 0}};

    EXPECT_CALL(*mockClient, getRubbishChildren(::testing::_, ::testing::_))
        .Times(2)
        .WillRepeatedly(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(binned)));
    EXPECT_CALL(*mockClient, getChildren(7, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{})));
    // The bug this guards: the Cloud Drive root must not be read at all.
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_)).Times(0);

    FolderNavigationService service(mockClient);
    Captured opened, entered, up;
    service.openRubbish(SortOrder{}, onDoneInto(opened));
    service.openFolder(7, SortOrder{}, onDoneInto(entered));
    ASSERT_TRUE(entered.doneResult.success);

    // What goUp() does with the breadcrumb's bin-top segment.
    service.navigateTo(0, true, ViewKind::Rubbish, SortOrder{}, onDoneInto(up));

    ASSERT_TRUE(up.doneCalled);
    EXPECT_TRUE(up.doneResult.success);
    EXPECT_EQ(service.currentLocation().kind, ViewKind::Rubbish);
    EXPECT_TRUE(service.currentLocation().isRoot);
}

TEST(FolderNavigationServiceTest, RefreshingInsideTheBinReadsTheFolderNotTheBinTop)
{
    // The bin top and a folder inside it are both kind Rubbish, so refreshCurrent
    // has to split them on isRoot or a refresh would jump back to the top.
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> binned{{"folder", 7, 0, true, 0}};

    EXPECT_CALL(*mockClient, getRubbishChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(binned)));
    EXPECT_CALL(*mockClient, getChildren(7, ::testing::_, ::testing::_))
        .Times(2)
        .WillRepeatedly(::testing::InvokeArgument<2>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{})));

    FolderNavigationService service(mockClient);
    Captured opened, entered, refreshed;
    service.openRubbish(SortOrder{}, onDoneInto(opened));
    service.openFolder(7, SortOrder{}, onDoneInto(entered));
    ASSERT_TRUE(entered.doneResult.success);

    service.refreshCurrent(SortOrder{}, onDoneInto(refreshed));

    ASSERT_TRUE(refreshed.doneCalled);
    EXPECT_TRUE(refreshed.doneResult.success);
}

TEST(FolderNavigationServiceTest, GoBackReturnsToTheFavouritesListingAFolderWasOpenedFrom)
{
    // The requirement the whole ViewKind-in-Location design exists for: opening a
    // folder out of the favourites listing and pressing Back lands on the listing
    // again, not on the folder that was showing before it.
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> favourites{{"trip", 2, 0, true, 0, false, true}};
    const std::vector<FileEntry> tripChildren{{"b.jpg", 3, 1, false, 0}};
    const std::vector<FileEntry> photoChildren{{"a.jpg", 4, 1, false, 0}};

    EXPECT_CALL(*mockClient, getChildren(1, ::testing::_, ::testing::_))
        .Times(2)
        .WillRepeatedly(
            ::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(photoChildren)));
    EXPECT_CALL(*mockClient, getChildren(2, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(tripChildren)));
    EXPECT_CALL(*mockClient, listFavourites(::testing::_, "", ::testing::_))
        .Times(2)
        .WillRepeatedly(
            ::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(favourites)));

    FolderNavigationService service(mockClient);
    Captured c1, c2, c3;
    service.openFolder(1, SortOrder{}, onDoneInto(c1)); // Cloud Drive/photos
    ASSERT_TRUE(c1.doneResult.success);
    service.openFavourites(SortOrder{}, onDoneInto(c2)); // the favourites listing
    ASSERT_TRUE(c2.doneResult.success);
    service.openFolder(2, SortOrder{}, onDoneInto(c3)); // a folder from that listing
    ASSERT_TRUE(c3.doneResult.success);
    ASSERT_EQ(service.currentLocation().kind, ViewKind::CloudDrive);

    // Act
    Captured firstBack;
    service.goBack(SortOrder{}, onDoneInto(firstBack));

    // Assert: back on the listing, re-queried rather than restored from a handle
    ASSERT_TRUE(firstBack.doneCalled);
    EXPECT_TRUE(firstBack.doneResult.success);
    EXPECT_EQ(firstBack.doneResult.value(), favourites);
    EXPECT_EQ(service.currentLocation().kind, ViewKind::Favourites);

    // Act: one more Back leaves the listing for the folder underneath it
    Captured secondBack;
    service.goBack(SortOrder{}, onDoneInto(secondBack));

    // Assert
    ASSERT_TRUE(secondBack.doneResult.success);
    EXPECT_EQ(secondBack.doneResult.value(), photoChildren);
    EXPECT_EQ(service.currentLocation().kind, ViewKind::CloudDrive);
    EXPECT_EQ(service.currentLocation().handle, 1u);
}

TEST(FolderNavigationServiceTest, RefreshCurrentInFavouritesReQueriesTheFavourites)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> favourites{{"kept.txt", 5, 1, false, 0, false, true}};

    EXPECT_CALL(*mockClient, listFavourites(::testing::_, "", ::testing::_))
        .Times(2)
        .WillRepeatedly(
            ::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(favourites)));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_)).Times(0);
    EXPECT_CALL(*mockClient, getChildren(::testing::_, ::testing::_, ::testing::_)).Times(0);

    FolderNavigationService service(mockClient);
    Captured opened;
    service.openFavourites(SortOrder{}, onDoneInto(opened));
    ASSERT_TRUE(opened.doneResult.success);

    // Act
    Captured refreshed;
    service.refreshCurrent(SortOrder{}, onDoneInto(refreshed));

    // Assert
    ASSERT_TRUE(refreshed.doneCalled);
    EXPECT_TRUE(refreshed.doneResult.success);
    EXPECT_EQ(service.currentLocation().kind, ViewKind::Favourites);
}

TEST(FolderNavigationServiceTest, ResolveCurrentPathInFavouritesSynthesizesOneNamelessSegment)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> favourites{{"kept.txt", 5, 1, false, 0, false, true}};

    EXPECT_CALL(*mockClient, listFavourites(::testing::_, "", ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(favourites)));
    // There is no ancestor chain to ask the SDK for.
    EXPECT_CALL(*mockClient, getPath(::testing::_, ::testing::_, ::testing::_)).Times(0);

    FolderNavigationService service(mockClient);
    Captured opened;
    service.openFavourites(SortOrder{}, onDoneInto(opened));
    ASSERT_TRUE(opened.doneResult.success);

    // Act
    CapturedPath captured;
    service.resolveCurrentPath(onPathDoneInto(captured));

    // Assert: nameless on purpose -- QML owns the label, as it does the root's
    ASSERT_TRUE(captured.doneCalled);
    ASSERT_TRUE(captured.doneResult.success);
    const std::vector<PathSegment> expected{{"", 0, false, ViewKind::Favourites}};
    EXPECT_EQ(captured.doneResult.value(), expected);
}

TEST(FolderNavigationServiceTest, ResetToRootDropsTheFavouritesLocation)
{
    // The logout path: a later login must not resume on the previous account's
    // favourites, nor keep its handles on the back-stack.
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> favourites{{"kept.txt", 5, 1, false, 0, false, true}};

    EXPECT_CALL(*mockClient, listFavourites(::testing::_, "", ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(favourites)));

    FolderNavigationService service(mockClient);
    Captured opened;
    service.openFavourites(SortOrder{}, onDoneInto(opened));
    ASSERT_TRUE(opened.doneResult.success);

    // Act
    service.resetToRoot();

    // Assert
    EXPECT_EQ(service.currentLocation().kind, ViewKind::CloudDrive);
    EXPECT_TRUE(service.currentLocation().isRoot);
    EXPECT_FALSE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, ListFavouritesForwardsTheNameFilterAndTouchesNoState)
{
    // The read-only counterpart, which is how the search box narrows a favourites
    // listing instead of searching a folder.
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> matches{{"kept.txt", 5, 1, false, 0, false, true}};

    EXPECT_CALL(*mockClient, listFavourites(::testing::_, "kept", ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(matches)));

    FolderNavigationService service(mockClient);
    Captured captured;

    // Act
    service.listFavourites(SortOrder{}, "kept", onDoneInto(captured));

    // Assert
    ASSERT_TRUE(captured.doneCalled);
    EXPECT_TRUE(captured.doneResult.success);
    EXPECT_EQ(service.currentLocation().kind, ViewKind::CloudDrive);
    EXPECT_TRUE(service.currentLocation().isRoot);
    EXPECT_FALSE(service.canGoBack());
}

// --- Recents, the other location that is not a folder ------------------------

TEST(FolderNavigationServiceTest, OpenRecentsPushesTheCurrentLocationAndSwitchesKind)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    const std::vector<FileEntry> recent{{"added.txt", 5, 1, false, 0, false, false}};

    EXPECT_CALL(*mockClient, listRecent(::testing::_, "", ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<std::vector<FileEntry>>::ok(recent)));

    FolderNavigationService service(mockClient);
    Captured captured;

    // Act
    service.openRecents(SortOrder{}, onDoneInto(captured));

    // Assert
    ASSERT_TRUE(captured.doneCalled);
    EXPECT_TRUE(captured.doneResult.success);
    EXPECT_EQ(captured.doneResult.value().size(), recent.size());
    EXPECT_EQ(service.currentLocation().kind, ViewKind::Recents);
    EXPECT_TRUE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, OpenRecentsWhileAlreadyThereRefetchesWithoutPushing)
{
    // Arrange: the same guard the favourites row relies on -- clicking the side-panel
    // row from the screen it opens must not stack a repeat for Back to walk down.
    auto mockClient = std::make_shared<MockMegaClient>();

    EXPECT_CALL(*mockClient, listRecent(::testing::_, "", ::testing::_))
        .Times(2)
        .WillRepeatedly(::testing::InvokeArgument<2>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{})));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{})));

    FolderNavigationService service(mockClient);
    Captured first, second;
    service.openRecents(SortOrder{}, onDoneInto(first));
    ASSERT_TRUE(first.doneResult.success);

    // Act
    service.openRecents(SortOrder{}, onDoneInto(second));

    // Assert
    ASSERT_TRUE(second.doneResult.success);
    EXPECT_EQ(service.currentLocation().kind, ViewKind::Recents);
    ASSERT_TRUE(service.canGoBack());

    Captured back;
    service.goBack(SortOrder{}, onDoneInto(back));
    ASSERT_TRUE(back.doneResult.success);
    EXPECT_EQ(service.currentLocation().kind, ViewKind::CloudDrive);
    EXPECT_FALSE(service.canGoBack());
}

TEST(FolderNavigationServiceTest, GoBackReturnsToTheRecentsListingAFolderWasOpenedFrom)
{
    // Back has to re-issue the query rather than ask for a folder's children: the
    // recents listing has no handle to go back to.
    auto mockClient = std::make_shared<MockMegaClient>();

    EXPECT_CALL(*mockClient, listRecent(::testing::_, "", ::testing::_))
        .Times(2)
        .WillRepeatedly(::testing::InvokeArgument<2>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{})));
    EXPECT_CALL(*mockClient, getChildren(42u, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{})));

    FolderNavigationService service(mockClient);
    Captured opened, entered;
    service.openRecents(SortOrder{}, onDoneInto(opened));
    service.openFolder(42, SortOrder{}, onDoneInto(entered));
    ASSERT_TRUE(entered.doneResult.success);

    // Act
    Captured back;
    service.goBack(SortOrder{}, onDoneInto(back));

    // Assert
    ASSERT_TRUE(back.doneResult.success);
    EXPECT_EQ(service.currentLocation().kind, ViewKind::Recents);
}

TEST(FolderNavigationServiceTest, RefreshCurrentInRecentsReQueriesTheRecents)
{
    auto mockClient = std::make_shared<MockMegaClient>();

    EXPECT_CALL(*mockClient, listRecent(::testing::_, "", ::testing::_))
        .Times(2)
        .WillRepeatedly(::testing::InvokeArgument<2>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{})));
    EXPECT_CALL(*mockClient, getRootChildren(::testing::_, ::testing::_)).Times(0);

    FolderNavigationService service(mockClient);
    Captured opened;
    service.openRecents(SortOrder{}, onDoneInto(opened));
    ASSERT_TRUE(opened.doneResult.success);

    // Act
    Captured refreshed;
    service.refreshCurrent(SortOrder{}, onDoneInto(refreshed));

    // Assert
    ASSERT_TRUE(refreshed.doneResult.success);
    EXPECT_EQ(service.currentLocation().kind, ViewKind::Recents);
}

TEST(FolderNavigationServiceTest, ResolveCurrentPathInRecentsSynthesizesOneNamelessSegment)
{
    auto mockClient = std::make_shared<MockMegaClient>();

    EXPECT_CALL(*mockClient, listRecent(::testing::_, "", ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{})));
    EXPECT_CALL(*mockClient, getPath(::testing::_, ::testing::_, ::testing::_)).Times(0);

    FolderNavigationService service(mockClient);
    Captured opened;
    service.openRecents(SortOrder{}, onDoneInto(opened));
    ASSERT_TRUE(opened.doneResult.success);

    // Act
    CapturedPath captured;
    service.resolveCurrentPath(onPathDoneInto(captured));

    // Assert: the segment carries its own kind, which is what QML labels it from
    ASSERT_TRUE(captured.doneResult.success);
    const std::vector<PathSegment> expected{{"", 0, false, ViewKind::Recents}};
    EXPECT_EQ(captured.doneResult.value(), expected);
}

TEST(FolderNavigationServiceTest, ListRecentForwardsTheNameFilterAndTouchesNoState)
{
    auto mockClient = std::make_shared<MockMegaClient>();

    EXPECT_CALL(*mockClient, listRecent(::testing::_, "added", ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{})));

    FolderNavigationService service(mockClient);
    Captured captured;

    // Act
    service.listRecent(SortOrder{}, "added", onDoneInto(captured));

    // Assert
    ASSERT_TRUE(captured.doneResult.success);
    EXPECT_EQ(service.currentLocation().kind, ViewKind::CloudDrive);
    EXPECT_TRUE(service.currentLocation().isRoot);
    EXPECT_FALSE(service.canGoBack());
}
