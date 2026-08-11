#include "qml/FolderNavigationController.h"

#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"
#include "qml/BusyState.h"
#include "qml/GuiThread.h"
#include "qml/NotificationController.h"
#include "qml/ViewKindEnum.h"
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

    // The favourites listing is a query rather than a folder, so every arrival at
    // it -- opening it, Back returning to it, a refresh -- re-runs the query;
    // counting the calls is how "re-queried exactly once" gets asserted. Only the
    // unfiltered form is stubbed here: a test that narrows the listing sets its own
    // expectation for the filtered one.
    void givenFavourites(std::vector<FileEntry> entries)
    {
        favouriteListing = std::move(entries);
        EXPECT_CALL(*client, listFavourites(_, std::string(), _))
            .WillRepeatedly(
                Invoke([this](SortOrder,
                              const std::string&,
                              std::function<void(Result<std::vector<FileEntry>>)> onDone) {
                    ++favouriteFetches;
                    onDone(Result<std::vector<FileEntry>>::ok(favouriteListing));
                }));
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
    std::vector<FileEntry> favouriteListing;
    int favouriteFetches = 0;
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

TEST_F(FolderNavigationControllerTest, ViewKindIsCloudDriveWhetherOrNotTheBreadcrumbResolved)
{
    givenRootListing({entry("a", 1)});
    // The fixture's default resolves to an empty path; this one exercises the
    // other branch of viewKind(), where a real segment supplies the kind.
    EXPECT_CALL(*client, getPath(_, _, _))
        .WillRepeatedly(InvokeArgument<2>(
            Result<std::vector<PathSegment>>::ok(std::vector<PathSegment>{{"", 0, true}})));

    EXPECT_EQ(controller->viewKind(), ViewKindEnum::CloudDrive);

    controller->loadRoot();
    flush();

    EXPECT_EQ(controller->viewKind(), ViewKindEnum::CloudDrive);
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

TEST_F(FolderNavigationControllerTest, CanPerformAnswersForBothMenusThisViewCouldOpen)
{
    // The keyboard's gate (F3 wires Delete/Ctrl+X/Ctrl+V to it): a shortcut stands
    // in for a row of either the selection menu or the background one. The
    // Favourites side of the same axis is the test below this one.
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    FileListModel* model = qobject_cast<FileListModel*>(controller->fileListModel());
    ASSERT_NE(model, nullptr);

    EXPECT_FALSE(controller->canPerform("moveToRubbish")); // nothing selected
    EXPECT_TRUE(controller->canPerform("paste"));          // background site, no selection needed

    model->selectRow(0, Qt::NoModifier);

    EXPECT_TRUE(controller->canPerform("moveToRubbish"));
    EXPECT_TRUE(controller->canPerform("cut"));
    EXPECT_FALSE(controller->canPerform("noSuchAction"));
}

// --- Favourites --------------------------------------------------------------

TEST_F(FolderNavigationControllerTest, OpenFavouritesShowsTheFavouriteListing)
{
    givenFavourites({entry("kept.txt", 5), entry("trip", 6, true)});

    controller->openFavourites();
    flush();

    EXPECT_EQ(errorCalls, 0);
    EXPECT_EQ(favouriteFetches, 1);
    ASSERT_EQ(model()->rowCount(), 2);
    EXPECT_EQ(model()->entryAt(0).value(QStringLiteral("name")).toString(),
              QStringLiteral("kept.txt"));
}

TEST_F(FolderNavigationControllerTest, FavouritesResolveToOneSyntheticBreadcrumbSegment)
{
    givenFavourites({entry("kept.txt", 5)});
    // The listing has no ancestor chain, so the whole breadcrumb is synthesized
    // without the SDK -- which is what makes the four properties below derivable
    // from mBreadcrumb as usual, with no favourites-specific branch in any of them.
    EXPECT_CALL(*client, getPath(_, _, _)).Times(0);

    controller->openFavourites();
    flush();

    EXPECT_EQ(controller->viewKind(), ViewKindEnum::Favourites);
    ASSERT_EQ(controller->breadcrumb().size(), 1);
    EXPECT_EQ(controller->breadcrumb().first().toMap().value(QStringLiteral("kind")).toInt(),
              static_cast<int>(ViewKindEnum::Favourites));
    // One segment means no parent to go up to, and the segment is nameless
    // because QML owns the label.
    EXPECT_FALSE(controller->canGoUp());
    EXPECT_TRUE(controller->currentFolderName().isEmpty());
    EXPECT_EQ(controller->currentHandle(), 0u);
}

TEST_F(FolderNavigationControllerTest, SearchInFavouritesNarrowsTheListingWithoutTheSearchService)
{
    givenFavourites({entry("kept.txt", 5), entry("other.txt", 6)});
    controller->openFavourites();
    flush();

    // SearchService is defined as "recursive search under the current folder",
    // which a flat cross-drive listing has none of -- so the search box narrows
    // the favourites query instead of reaching the service at all.
    EXPECT_CALL(*client, search(_, _, _, _, _)).Times(0);
    int filtered = 0;
    EXPECT_CALL(*client, listFavourites(_, std::string("kept"), _))
        .WillRepeatedly(
            Invoke([&filtered](SortOrder,
                               const std::string&,
                               std::function<void(Result<std::vector<FileEntry>>)> onDone) {
                ++filtered;
                onDone(Result<std::vector<FileEntry>>::ok({entry("kept.txt", 5)}));
            }));

    controller->search(QStringLiteral("kept"));
    flush();

    EXPECT_EQ(filtered, 1);
    ASSERT_EQ(model()->rowCount(), 1);
    EXPECT_EQ(model()->entryAt(0).value(QStringLiteral("name")).toString(),
              QStringLiteral("kept.txt"));
}

TEST_F(FolderNavigationControllerTest, ClearingTheSearchInFavouritesRestoresTheFullListing)
{
    givenFavourites({entry("kept.txt", 5), entry("other.txt", 6)});
    controller->openFavourites();
    flush();

    EXPECT_CALL(*client, listFavourites(_, std::string("kept"), _))
        .WillRepeatedly(InvokeArgument<2>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{entry("kept.txt", 5)})));
    controller->search(QStringLiteral("kept"));
    flush();
    ASSERT_EQ(model()->rowCount(), 1);
    const int fetchesBefore = favouriteFetches;

    controller->search(QString());
    flush();

    // Restored from the cached listing, exactly as in a folder: no round-trip.
    EXPECT_EQ(model()->rowCount(), 2);
    EXPECT_EQ(favouriteFetches - fetchesBefore, 0);
}

TEST_F(FolderNavigationControllerTest, RefreshInFavouritesReQueriesTheFavourites)
{
    givenFavourites({entry("kept.txt", 5)});
    controller->openFavourites();
    flush();
    const int fetchesBefore = favouriteFetches;

    controller->refresh();
    flush();

    EXPECT_EQ(errorCalls, 0);
    EXPECT_EQ(favouriteFetches - fetchesBefore, 1);
}

TEST_F(FolderNavigationControllerTest, BackFromAFolderOpenedInFavouritesReturnsToTheListing)
{
    givenRootListing({entry("photos", 1, true)});
    givenFavourites({entry("trip", 2, true)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, getChildren(2, _, _))
        .WillRepeatedly(InvokeArgument<2>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{entry("b.jpg", 3)})));
    EXPECT_CALL(*client, getPath(2, false, _))
        .WillRepeatedly(InvokeArgument<2>(Result<std::vector<PathSegment>>::ok(
            std::vector<PathSegment>{{"", 0, true}, {"trip", 2, false}})));

    controller->openFavourites();
    flush();
    ASSERT_EQ(controller->viewKind(), ViewKindEnum::Favourites);
    controller->openFolder(2);
    flush();
    ASSERT_EQ(controller->viewKind(), ViewKindEnum::CloudDrive);

    controller->goBack();
    flush();

    // The breadcrumb follows the back-stack's view kind, so the tab is showing
    // the listing again rather than the folder that preceded it.
    EXPECT_EQ(controller->viewKind(), ViewKindEnum::Favourites);
    ASSERT_EQ(controller->breadcrumb().size(), 1);
    ASSERT_EQ(model()->rowCount(), 1);
    EXPECT_EQ(model()->entryAt(0).value(QStringLiteral("name")).toString(),
              QStringLiteral("trip"));
}

TEST_F(FolderNavigationControllerTest, CanPerformWithholdsDestinationActionsInFavourites)
{
    // The Favourites side of the scope axis, reachable now that the controller can
    // arrive there: a flat cross-drive listing is no destination, so the four
    // actions needing one are withheld from the keyboard as well as the menu.
    givenFavourites({entry("kept.txt", 5)});
    controller->openFavourites();
    flush();

    FileListModel* fileModel = qobject_cast<FileListModel*>(controller->fileListModel());
    ASSERT_NE(fileModel, nullptr);
    fileModel->selectRow(0, Qt::NoModifier);

    EXPECT_FALSE(controller->canPerform("paste"));
    EXPECT_FALSE(controller->canPerform("moveToRubbish"));
    EXPECT_FALSE(controller->canPerform("cut"));
    EXPECT_TRUE(controller->canPerform("copy"));
    EXPECT_TRUE(controller->canPerform("rename"));
}
