#include "qml/FolderNavigationController.h"

#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"
#include "qml/BusyState.h"
#include "qml/GuiThread.h"
#include "qml/NotificationController.h"
#include "TestApp.h"

#include <QEventLoop>
#include <QTimer>
#include <QVariantMap>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::_;
using ::testing::Invoke;
using ::testing::InvokeArgument;
using ::testing::Return;

namespace
{

// BusyState only publishes itself once its delay timer has fired, so a busy
// test has to let real time pass rather than just drain the queue. Deliberately
// not QSignalSpy: that lives in Qt6::Test, which this target doesn't link (see
// UploadControllerTest's note on the same choice).
constexpr int kBusyWaitTimeoutMs = 2000;

bool waitForBusy(BusyState& busy, bool expected)
{
    if (busy.visible() == expected)
        return true;

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&busy, &BusyState::changed, &loop, [&busy, expected, &loop]() {
        if (busy.visible() == expected)
            loop.quit();
    });
    timeout.start(kBusyWaitTimeoutMs);
    loop.exec();
    return busy.visible() == expected;
}

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
        client = std::make_shared<MockMegaClient>();
        navigationService = std::make_shared<FolderNavigationService>(client);
        searchService = std::make_shared<SearchService>(client, navigationService);
        notifications = std::make_unique<NotificationController>();
        // makeGuiOwned like main.cpp -- see TabsControllerTest's note.
        busy = makeGuiOwned<BusyState>();
        controller = makeGuiOwned<FolderNavigationController>(navigationService,
                                                             searchService,
                                                             busy,
                                                             notifications.get());

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
                         [this](QString context,
                                NotificationController::ErrorReason reason,
                                QString rawMessage) {
                             ++errorCalls;
                             lastErrorContext = context;
                             lastErrorReason = reason;
                             lastErrorRaw = rawMessage;
                         });

        // refresh() syncs with the API before re-reading the listing. Left
        // unstubbed, gMock's default for a void method swallows the callback
        // and the re-read never happens -- so this default has to exist for
        // any refresh test to get past the sync. A test that wants the sync
        // to fail sets its own EXPECT_CALL, which gMock matches first.
        EXPECT_CALL(*client, syncPendingChanges(_))
            .WillRepeatedly(InvokeArgument<0>(Result<void>::ok()));
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
        return controller->fileListModelForThumbnails().get();
    }

    std::shared_ptr<MockMegaClient> client;
    std::shared_ptr<FolderNavigationService> navigationService;
    std::shared_ptr<SearchService> searchService;
    std::unique_ptr<NotificationController> notifications;
    std::shared_ptr<BusyState> busy;
    std::shared_ptr<FolderNavigationController> controller;

    std::vector<FileEntry> rootListing;
    int rootFetches = 0;
    int operationCalls = 0;
    int errorCalls = 0;
    QString lastContext;
    QString lastErrorContext;
    NotificationController::ErrorReason lastErrorReason = NotificationController::Unknown;
    QString lastErrorRaw;
    int lastSucceeded = 0;
    int lastFailed = 0;
};

} // namespace

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

TEST_F(FolderNavigationControllerTest, RefreshSyncsWithTheServerBeforeReReadingTheListing)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    // Held rather than answered, so the ordering is observable: the listing
    // must not be re-read until the sync says the node tree is current.
    std::function<void(Result<void>)> pendingSync;
    EXPECT_CALL(*client, syncPendingChanges(_))
        .WillOnce(Invoke([&pendingSync](std::function<void(Result<void>)> onDone) {
            pendingSync = std::move(onDone);
        }));
    const int fetchesBefore = rootFetches;

    controller->refresh();
    flush();
    ASSERT_TRUE(pendingSync);
    EXPECT_EQ(rootFetches - fetchesBefore, 0);

    pendingSync(Result<void>::ok());
    flush();

    EXPECT_EQ(rootFetches - fetchesBefore, 1);
}

TEST_F(FolderNavigationControllerTest, RefreshStillReReadsTheListingWhenTheSyncFails)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, syncPendingChanges(_))
        .WillOnce(InvokeArgument<0>(Result<void>::fail("offline", MegaErrorCode::kEAgain)));
    const int fetchesBefore = rootFetches;

    controller->refresh();
    flush();

    // Reported, but the local listing is re-read anyway -- the user asked for
    // a refresh, and what the SDK already has is still worth showing.
    EXPECT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("refresh"));
    // The code reaches the toast as a reason, so ToastStack can say something
    // other than "offline" in English. R3-5's gap was on the QML side -- this
    // only locks the C++ half of it.
    EXPECT_EQ(lastErrorReason, NotificationController::Offline);
    EXPECT_TRUE(lastErrorRaw.isEmpty());
    EXPECT_EQ(rootFetches - fetchesBefore, 1);
}

TEST_F(FolderNavigationControllerTest, RefreshIfShowingDoesNotSyncWithTheServer)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    // Its callers are reporting a change this app just made, so the SDK
    // already knows about it: syncing once per showing tab would be that many
    // pointless round-trips.
    EXPECT_CALL(*client, syncPendingChanges(_)).Times(0);
    const int fetchesBefore = rootFetches;

    controller->refreshIfShowing(0, true);
    flush();

    EXPECT_EQ(rootFetches - fetchesBefore, 1);
}

// The only begin/end pair left on this side after R5-1 moved the mutations out
// (which is what the three old busy tests all drove). Holds the sync callback
// so the spinner is observable while it is outstanding, the way
// RefreshSyncsWithTheServerBeforeReReadingTheListing above holds it to observe
// the ordering.
TEST_F(FolderNavigationControllerTest, RefreshHoldsBusyUntilTheServerSyncAnswers)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    std::function<void(Result<void>)> pendingSync;
    EXPECT_CALL(*client, syncPendingChanges(_))
        .WillOnce(Invoke([&pendingSync](std::function<void(Result<void>)> onDone) {
            pendingSync = std::move(onDone);
        }));

    controller->refresh();
    flush();
    ASSERT_TRUE(pendingSync);
    ASSERT_TRUE(waitForBusy(*busy, true));

    pendingSync(Result<void>::ok());
    flush();

    EXPECT_TRUE(waitForBusy(*busy, false));
}
