#include "qml/FolderNavigationController.h"

#include "MockMegaClient.h"
#include "qml/NotificationController.h"
#include "TestApp.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::_;
using ::testing::Invoke;
using ::testing::InvokeArgument;

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
