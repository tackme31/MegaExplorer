#include "qml/FolderNavigationController.h"

#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"
#include "qml/NotificationController.h"
#include "TestApp.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::_;
using ::testing::Invoke;
using ::testing::InvokeArgument;
using ::testing::Return;

// FolderNavigationController is src/qml GUI glue, which this codebase otherwise
// leaves untested by convention -- bent here for exactly one thing, the same way
// FileListModel/TabsController/QuickAccessModel bend it:
// moveSelectionToRubbish()'s N-way fan-out has to collapse N independent SDK
// results into exactly one refetch and one notification, which is
// bookkeeping, not rendering. Everything else in this class stays untested.
namespace
{

FileEntry entry(const char* name, std::uint64_t handle, bool isFolder = false)
{
    FileEntry e;
    e.name = name;
    e.handle = handle;
    e.isFolder = isFolder;
    return e;
}

class FolderNavigationControllerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        testApp();
        client = std::make_shared<MockMegaClient>();
        navigationService = std::make_shared<FolderNavigationService>(client);
        searchService = std::make_shared<SearchService>(client, navigationService);
        fileOps = std::make_shared<FileOperationService>(client);
        notifications = std::make_unique<NotificationController>();
        controller = std::make_shared<FolderNavigationController>(
            navigationService, searchService, fileOps, notifications.get());

        QObject::connect(notifications.get(),
                         &NotificationController::operationFinished,
                         notifications.get(),
                         [this](QString context, int succeeded, int failed) {
                             ++operationCalls;
                             lastContext = context;
                             lastSucceeded = succeeded;
                             lastFailed = failed;
                         });
        QObject::connect(notifications.get(),
                         &NotificationController::errorOccurred,
                         notifications.get(),
                         [this](QString context, QString) {
                             ++errorCalls;
                             lastErrorContext = context;
                         });
    }

    // Every folder fetch in these tests happens at the root, so counting
    // getRootChildren calls is how "the listing was refetched exactly once"
    // gets asserted.
    void givenRootListing(std::vector<FileEntry> entries)
    {
        rootListing = std::move(entries);
        EXPECT_CALL(*client, getRootChildren(_, _))
            .WillRepeatedly(Invoke(
                [this](SortOrder, std::function<void(Result<std::vector<FileEntry>>)> onDone) {
                    ++rootFetches;
                    onDone(Result<std::vector<FileEntry>>::ok(rootListing));
                }));
        // refreshBreadcrumb runs after every successful listing; an empty path
        // leaves the breadcrumb untouched, which is all these tests need.
        EXPECT_CALL(*client, getPath(_, _, _))
            .WillRepeatedly(InvokeArgument<2>(
                Result<std::vector<PathSegment>>::ok(std::vector<PathSegment>{})));
    }

    // A queued invoke can post another one (the refetch a mutation triggers),
    // so one drain isn't necessarily enough.
    static void flush()
    {
        flushQueuedEvents();
        flushQueuedEvents();
    }

    FileListModel* model()
    {
        return controller->fileListModelForThumbnails();
    }

    std::shared_ptr<MockMegaClient> client;
    std::shared_ptr<FolderNavigationService> navigationService;
    std::shared_ptr<SearchService> searchService;
    std::shared_ptr<FileOperationService> fileOps;
    std::unique_ptr<NotificationController> notifications;
    std::shared_ptr<FolderNavigationController> controller;

    std::vector<FileEntry> rootListing;
    int rootFetches = 0;
    int operationCalls = 0;
    int errorCalls = 0;
    QString lastContext;
    QString lastErrorContext;
    int lastSucceeded = 0;
    int lastFailed = 0;
};

} // namespace

TEST_F(FolderNavigationControllerTest, MoveSelectionToRubbishReportsOneTallyForTheWholeSelection)
{
    givenRootListing({entry("a", 1), entry("b", 2, true), entry("c", 3)});
    controller->loadRoot();
    flush();
    ASSERT_EQ(model()->rowCount(), 3);
    model()->selectAll();

    EXPECT_CALL(*client, moveToRubbish(_, _))
        .Times(3)
        .WillRepeatedly(InvokeArgument<1>(Result<void>::ok()));
    const int fetchesBefore = rootFetches;

    controller->moveSelectionToRubbish();
    flush();

    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastContext, QStringLiteral("moveToRubbish"));
    EXPECT_EQ(lastSucceeded, 3);
    EXPECT_EQ(lastFailed, 0);
    // One refetch for the batch, not one per item.
    EXPECT_EQ(rootFetches - fetchesBefore, 1);
}

TEST_F(FolderNavigationControllerTest, MoveSelectionToRubbishSeparatesSucceededFromFailed)
{
    givenRootListing({entry("a", 1), entry("b", 2), entry("c", 3)});
    controller->loadRoot();
    flush();
    model()->selectAll();

    EXPECT_CALL(*client, moveToRubbish(1u, _)).WillOnce(InvokeArgument<1>(Result<void>::ok()));
    EXPECT_CALL(*client, moveToRubbish(2u, _))
        .WillOnce(InvokeArgument<1>(Result<void>::fail("access denied")));
    EXPECT_CALL(*client, moveToRubbish(3u, _)).WillOnce(InvokeArgument<1>(Result<void>::ok()));
    const int fetchesBefore = rootFetches;

    controller->moveSelectionToRubbish();
    flush();

    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastSucceeded, 2);
    EXPECT_EQ(lastFailed, 1);
    // A partial failure still refreshes -- the ones that worked are gone.
    EXPECT_EQ(rootFetches - fetchesBefore, 1);
}

TEST_F(FolderNavigationControllerTest, MoveSelectionToRubbishHandlesASingleItem)
{
    givenRootListing({entry("a", 1), entry("b", 2)});
    controller->loadRoot();
    flush();
    model()->selectRow(1, Qt::NoModifier);

    EXPECT_CALL(*client, moveToRubbish(2u, _)).WillOnce(InvokeArgument<1>(Result<void>::ok()));
    const int fetchesBefore = rootFetches;

    controller->moveSelectionToRubbish();
    flush();

    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastSucceeded, 1);
    EXPECT_EQ(lastFailed, 0);
    EXPECT_EQ(rootFetches - fetchesBefore, 1);
}

TEST_F(FolderNavigationControllerTest, MoveSelectionToRubbishDoesNothingWithAnEmptySelection)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();
    model()->clearSelection();

    EXPECT_CALL(*client, moveToRubbish(_, _)).Times(0);
    const int fetchesBefore = rootFetches;

    controller->moveSelectionToRubbish();
    flush();

    EXPECT_EQ(operationCalls, 0);
    EXPECT_EQ(rootFetches - fetchesBefore, 0);
}

TEST_F(FolderNavigationControllerTest, MoveHandlesToReportsOneTallyForTheWholeDrop)
{
    givenRootListing({entry("a", 1), entry("b", 2), entry("c", 3)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, checkMove(_, 99u, false)).WillRepeatedly(Return(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(_, 99u, false, _))
        .Times(3)
        .WillRepeatedly(InvokeArgument<3>(Result<void>::ok()));
    const int fetchesBefore = rootFetches;

    controller->moveHandlesTo({1u, 2u, 3u}, 99, false);
    flush();

    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastContext, QStringLiteral("move"));
    EXPECT_EQ(lastSucceeded, 3);
    EXPECT_EQ(lastFailed, 0);
    // One refetch for the whole drop, not one per item.
    EXPECT_EQ(rootFetches - fetchesBefore, 1);
}

TEST_F(FolderNavigationControllerTest, MoveHandlesToSeparatesSucceededFromFailed)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, checkMove(_, _, _)).WillRepeatedly(Return(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(1u, _, _, _)).WillOnce(InvokeArgument<3>(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(2u, _, _, _))
        .WillOnce(InvokeArgument<3>(Result<void>::fail("access denied")));

    controller->moveHandlesTo({1u, 2u}, 99, false);
    flush();

    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastSucceeded, 1);
    EXPECT_EQ(lastFailed, 1);
}

TEST_F(FolderNavigationControllerTest, MoveHandlesToForwardsTheRootSentinel)
{
    // Dropping onto the tree's "Cloud Drive" row: the handle is meaningless and
    // only the isRoot flag identifies the destination.
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, checkMove(1u, _, true)).WillOnce(Return(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(1u, _, true, _)).WillOnce(InvokeArgument<3>(Result<void>::ok()));

    controller->moveHandlesTo({1u}, 0, true);
    flush();

    EXPECT_EQ(lastSucceeded, 1);
}

TEST_F(FolderNavigationControllerTest, MoveHandlesToDoesNothingWithAnEmptyDrop)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, moveNode(_, _, _, _)).Times(0);
    const int fetchesBefore = rootFetches;

    controller->moveHandlesTo({}, 99, false);
    flush();

    EXPECT_EQ(operationCalls, 0);
    EXPECT_EQ(rootFetches - fetchesBefore, 0);
}

TEST_F(FolderNavigationControllerTest, CanDropHandlesOnRejectsUnlessEveryHandlePasses)
{
    // A drop target is all-or-nothing: one un-movable item greys out the whole
    // drop rather than silently moving the rest.
    EXPECT_CALL(*client, checkMove(1u, 99u, false)).WillRepeatedly(Return(Result<void>::ok()));
    EXPECT_CALL(*client, checkMove(2u, 99u, false))
        .WillRepeatedly(Return(Result<void>::fail("circular", MegaErrorCode::kECircular)));

    EXPECT_TRUE(controller->canDropHandlesOn({1u}, 99, false));
    EXPECT_FALSE(controller->canDropHandlesOn({1u, 2u}, 99, false));
}

TEST_F(FolderNavigationControllerTest, CanDropHandlesOnRejectsAnEmptyDrag)
{
    EXPECT_CALL(*client, checkMove(_, _, _)).Times(0);

    EXPECT_FALSE(controller->canDropHandlesOn({}, 99, false));
}

TEST_F(FolderNavigationControllerTest, RenameEntryReportsAnInvalidNameWithoutRefetching)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    // Rejected by FileOperationService's own validation, so the SDK is never
    // reached and the listing is left alone.
    EXPECT_CALL(*client, renameNode(_, _, _)).Times(0);
    const int fetchesBefore = rootFetches;

    controller->renameEntry(1, QStringLiteral("bad/name"));
    flush();

    EXPECT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("rename"));
    EXPECT_EQ(rootFetches - fetchesBefore, 0);
}

TEST_F(FolderNavigationControllerTest, RenameEntryRefetchesOnSuccess)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, renameNode(1u, std::string("b"), _))
        .WillOnce(InvokeArgument<2>(Result<void>::ok()));
    const int fetchesBefore = rootFetches;

    controller->renameEntry(1, QStringLiteral("b"));
    flush();

    EXPECT_EQ(errorCalls, 0);
    EXPECT_EQ(rootFetches - fetchesBefore, 1);
}

TEST_F(FolderNavigationControllerTest, RefreshRefetchesTheCurrentFolder)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    const int fetchesBefore = rootFetches;

    controller->refresh();
    flush();

    EXPECT_EQ(errorCalls, 0);
    EXPECT_EQ(rootFetches - fetchesBefore, 1);
}

TEST_F(FolderNavigationControllerTest, RefreshDoesNothingBeforeTheFirstLoad)
{
    givenRootListing({entry("a", 1)});

    controller->refresh();
    flush();

    EXPECT_EQ(rootFetches, 0);
}

TEST_F(FolderNavigationControllerTest, RefreshReRunsTheActiveSearch)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    int searches = 0;
    EXPECT_CALL(*client, search(_, _, std::string("q"), _, _))
        .WillRepeatedly(
            Invoke([&searches](std::uint64_t,
                               bool,
                               const std::string&,
                               SortOrder,
                               std::function<void(Result<std::vector<FileEntry>>)> onDone) {
                ++searches;
                onDone(Result<std::vector<FileEntry>>::ok({entry("a", 1)}));
            }));

    controller->search(QStringLiteral("q"));
    flush();
    const int searchesBefore = searches;
    const int fetchesBefore = rootFetches;

    controller->refresh();
    flush();

    // The search is re-run rather than dropped, and the folder listing behind
    // it is refreshed too so clearing the search afterwards isn't stale.
    EXPECT_EQ(searches - searchesBefore, 1);
    EXPECT_EQ(rootFetches - fetchesBefore, 1);
}
