#include "qml/FolderNavigationController.h"

#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"
#include "qml/ClipboardController.h"
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

// FolderNavigationController is src/qml GUI glue, which this codebase otherwise
// leaves untested by convention -- bent here for exactly one thing, the same way
// FileListModel/TabsController/QuickAccessModel bend it:
// moveSelectionToRubbish()'s N-way fan-out has to collapse N independent SDK
// results into exactly one refetch and one notification, which is
// bookkeeping, not rendering. Everything else in this class stays untested.
namespace
{

// FolderNavigationController only publishes busy once its own delay timer has
// fired, so the busy tests have to let real time pass rather than just drain
// the queue. Deliberately not QSignalSpy: that lives in Qt6::Test, which this
// target doesn't link (see UploadControllerTest's note on the same choice).
constexpr int kBusyWaitTimeoutMs = 2000;

bool waitForBusy(FolderNavigationController& controller, bool expected)
{
    if (controller.busy() == expected)
        return true;

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&controller,
                     &FolderNavigationController::busyChanged,
                     &loop,
                     [&controller, expected, &loop]() {
                         if (controller.busy() == expected)
                             loop.quit();
                     });
    timeout.start(kBusyWaitTimeoutMs);
    loop.exec();
    return controller.busy() == expected;
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
        testApp();
        client = std::make_shared<MockMegaClient>();
        navigationService = std::make_shared<FolderNavigationService>(client);
        searchService = std::make_shared<SearchService>(client, navigationService);
        fileOps = std::make_shared<FileOperationService>(client);
        notifications = std::make_unique<NotificationController>();
        clipboard = std::make_unique<ClipboardController>();
        // makeGuiOwned like main.cpp -- see TabsControllerTest's note.
        controller = makeGuiOwned<FolderNavigationController>(
            navigationService, searchService, fileOps, notifications.get(), clipboard.get());

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

        // refresh() syncs with the API before re-reading the listing. Left
        // unstubbed, gMock's default for a void method swallows the callback
        // and the re-read never happens -- so this default has to exist for
        // any refresh test to get past the sync. A test that wants the sync
        // to fail sets its own EXPECT_CALL, which gMock matches first.
        EXPECT_CALL(*client, syncPendingChanges(_))
            .WillRepeatedly(InvokeArgument<0>(Result<void>::ok()));
        // Same reasoning, sharper edge: Result<void>::success defaults to
        // false, so an unstubbed paste pre-check *refuses* rather than merely
        // doing nothing. A test that wants it to fail sets its own EXPECT_CALL.
        EXPECT_CALL(*client, checkUpload(_, _)).WillRepeatedly(Return(Result<void>::ok()));
        // And again for the per-source half: FileOperationService::copy() gates
        // on canCopy(), which is checkMove reinterpreted, so an unstubbed
        // checkMove refuses every copy before it reaches copyNode.
        EXPECT_CALL(*client, checkMove(_, _, _)).WillRepeatedly(Return(Result<void>::ok()));
    }

    // Fills the clipboard the way QML does -- selectedEntries()-shaped maps.
    QVariantList clipboardEntries(std::vector<FileEntry> entries)
    {
        QVariantList list;
        for (const FileEntry& e : entries)
        {
            QVariantMap map;
            map[QStringLiteral("handle")] = static_cast<quint64>(e.handle);
            map[QStringLiteral("name")] = QString::fromStdString(e.name);
            map[QStringLiteral("isFolder")] = e.isFolder;
            list.append(map);
        }
        return list;
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
    std::unique_ptr<ClipboardController> clipboard;
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
        .WillOnce(InvokeArgument<0>(Result<void>::fail("offline")));
    const int fetchesBefore = rootFetches;

    controller->refresh();
    flush();

    // Reported, but the local listing is re-read anyway -- the user asked for
    // a refresh, and what the SDK already has is still worth showing.
    EXPECT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("refresh"));
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

TEST_F(FolderNavigationControllerTest, BusyClearsOnlyAfterTheLastCallbackOfABulkOperation)
{
    givenRootListing({entry("a", 1), entry("b", 2)});
    controller->loadRoot();
    flush();
    model()->selectAll();

    std::vector<std::function<void(Result<void>)>> pending;
    EXPECT_CALL(*client, moveToRubbish(_, _))
        .WillRepeatedly(Invoke([&pending](std::uint64_t, std::function<void(Result<void>)> onDone) {
            pending.push_back(std::move(onDone));
        }));

    controller->moveSelectionToRubbish();
    ASSERT_EQ(pending.size(), 2u);
    ASSERT_TRUE(waitForBusy(*controller, true));

    pending[0](Result<void>::ok());
    flush();
    EXPECT_TRUE(controller->busy());

    pending[1](Result<void>::ok());
    flush();
    EXPECT_FALSE(controller->busy());
}

TEST_F(FolderNavigationControllerTest, BusyClearsWhenAnOperationFails)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    std::function<void(Result<void>)> pending;
    EXPECT_CALL(*client, createFolder(_, _, std::string("x"), _))
        .WillOnce(Invoke(
            [&pending](
                std::uint64_t, bool, const std::string&, std::function<void(Result<void>)> onDone) {
                pending = std::move(onDone);
            }));

    controller->createFolder(QStringLiteral("x"));
    ASSERT_TRUE(pending);
    ASSERT_TRUE(waitForBusy(*controller, true));

    // The kEExist branch returns early and without a toast -- of createFolder's
    // four outcomes, the one most likely to leak the count if it were
    // decremented per-branch rather than once above them.
    pending(Result<void>::fail("already exists", MegaErrorCode::kEExist));
    flush();

    EXPECT_FALSE(controller->busy());
}

TEST_F(FolderNavigationControllerTest, ResetClearsBusyWithOperationsStillInFlight)
{
    givenRootListing({entry("a", 1), entry("b", 2)});
    controller->loadRoot();
    flush();
    model()->selectAll();

    std::vector<std::function<void(Result<void>)>> pending;
    EXPECT_CALL(*client, moveToRubbish(_, _))
        .WillRepeatedly(Invoke([&pending](std::uint64_t, std::function<void(Result<void>)> onDone) {
            pending.push_back(std::move(onDone));
        }));

    controller->moveSelectionToRubbish();
    ASSERT_TRUE(waitForBusy(*controller, true));

    controller->reset();
    EXPECT_FALSE(controller->busy());

    // The abandoned callbacks still land. They must not drive the count
    // negative, or no later operation would ever reach 1 and show a spinner.
    for (const auto& callback : pending)
        callback(Result<void>::ok());
    flush();
    EXPECT_FALSE(controller->busy());

    pending.clear();
    controller->loadRoot();
    flush();
    model()->selectAll();

    controller->moveSelectionToRubbish();
    EXPECT_TRUE(waitForBusy(*controller, true));
}

// Paste (Phase 23) is the same bookkeeping concern as the fan-outs above, plus
// one of its own: the destination's names have to be read *before* any copy
// goes out, since a colliding name silently versions over the existing file
// instead of landing beside it (IMegaClient::copyNode).

TEST_F(FolderNavigationControllerTest, PasteDoesNothingWithAnEmptyClipboard)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).Times(0);
    EXPECT_CALL(*client, moveNode(_, _, _, _)).Times(0);
    const int fetchesBefore = rootFetches;

    controller->paste();
    flush();

    EXPECT_EQ(operationCalls, 0);
    EXPECT_EQ(rootFetches - fetchesBefore, 0);
}

TEST_F(FolderNavigationControllerTest, PasteReadsTheDestinationBeforeCopying)
{
    givenRootListing({entry("a.txt", 1)});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("z.txt", 5)}), 7, false);

    const int fetchesBefore = rootFetches;
    int fetchesWhenCopied = -1;
    EXPECT_CALL(*client, copyNode(5u, _, _, _, _))
        .WillOnce(Invoke([&](std::uint64_t,
                             std::uint64_t,
                             bool,
                             const std::string&,
                             std::function<void(Result<void>)> onDone) {
            fetchesWhenCopied = rootFetches;
            onDone(Result<void>::ok());
        }));

    controller->paste();
    flush();
    flush();

    EXPECT_EQ(fetchesWhenCopied, fetchesBefore + 1);
}

TEST_F(FolderNavigationControllerTest, PasteCopiesEveryClipboardEntryAndReportsOneTally)
{
    givenRootListing({});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a", 1), entry("b", 2), entry("c", 3)}), 7, false);

    EXPECT_CALL(*client, copyNode(_, _, _, _, _))
        .Times(3)
        .WillRepeatedly(InvokeArgument<4>(Result<void>::ok()));

    controller->paste();
    flush();
    flush();

    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastContext, QStringLiteral("copy"));
    EXPECT_EQ(lastSucceeded, 3);
    EXPECT_EQ(lastFailed, 0);
}

TEST_F(FolderNavigationControllerTest, PasteSeparatesSucceededFromFailedCopies)
{
    givenRootListing({});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a", 1), entry("b", 2)}), 7, false);

    EXPECT_CALL(*client, copyNode(1u, _, _, _, _)).WillOnce(InvokeArgument<4>(Result<void>::ok()));
    EXPECT_CALL(*client, copyNode(2u, _, _, _, _))
        .WillOnce(InvokeArgument<4>(Result<void>::fail("gone", MegaErrorCode::kENoEnt)));

    controller->paste();
    flush();
    flush();

    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastSucceeded, 1);
    EXPECT_EQ(lastFailed, 1);
}

TEST_F(FolderNavigationControllerTest, PasteKeepsANonCollidingNameUnchanged)
{
    givenRootListing({entry("other.txt", 1)});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a.txt", 5)}), 7, false);

    // Empty name == "keep the source's", the only way to reach copyNode's
    // unnamed SDK overload.
    EXPECT_CALL(*client, copyNode(5u, _, true, std::string(), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));

    controller->paste();
    flush();
    flush();

    EXPECT_EQ(lastSucceeded, 1);
}

TEST_F(FolderNavigationControllerTest, PasteAutoRenamesOnlyTheCollidingEntry)
{
    givenRootListing({entry("a.txt", 1)});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a.txt", 5), entry("b.txt", 6)}), 7, false);

    EXPECT_CALL(*client, copyNode(5u, _, _, std::string("a - Copy.txt"), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));
    EXPECT_CALL(*client, copyNode(6u, _, _, std::string(), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));

    controller->paste();
    flush();
    flush();

    EXPECT_EQ(lastSucceeded, 2);
}

TEST_F(FolderNavigationControllerTest, PasteDoesNotHandOutTheSameGeneratedNameTwice)
{
    // MEGA allows duplicate siblings, so two clipboard entries really can share
    // a name -- and neither may be given the name the other just claimed.
    givenRootListing({entry("a.txt", 1)});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a.txt", 5), entry("a.txt", 6)}), 7, false);

    EXPECT_CALL(*client, copyNode(5u, _, _, std::string("a - Copy.txt"), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));
    EXPECT_CALL(*client, copyNode(6u, _, _, std::string("a - Copy (2).txt"), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));

    controller->paste();
    flush();
    flush();

    EXPECT_EQ(lastSucceeded, 2);
}

TEST_F(FolderNavigationControllerTest, PasteFallsBackToTheCachedListingWhenTheDestinationReadFails)
{
    givenRootListing({entry("a.txt", 1)});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a.txt", 5)}), 7, false);

    // Matched ahead of givenRootListing's expectation, so every read from here
    // on fails -- the paste has to fall back to what it already had.
    EXPECT_CALL(*client, getRootChildren(_, _))
        .WillRepeatedly(InvokeArgument<1>(Result<std::vector<FileEntry>>::fail("offline")));
    EXPECT_CALL(*client, copyNode(5u, _, _, std::string("a - Copy.txt"), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));

    controller->paste();
    flush();
    flush();

    EXPECT_EQ(lastSucceeded, 1);
}

TEST_F(FolderNavigationControllerTest, PasteMovesInsteadOfCopyingWhenTheClipboardHoldsACut)
{
    givenRootListing({});
    controller->loadRoot();
    flush();
    clipboard->cut(clipboardEntries({entry("a", 1)}), 7, false);

    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).Times(0);
    EXPECT_CALL(*client, checkMove(1u, _, true)).WillOnce(Return(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(1u, _, true, _)).WillOnce(InvokeArgument<3>(Result<void>::ok()));

    controller->paste();
    flush();
    flush();

    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastContext, QStringLiteral("move"));
    EXPECT_EQ(lastSucceeded, 1);
}

TEST_F(FolderNavigationControllerTest, PasteReportsTheClipboardsSourceFolderInNodesMoved)
{
    // The regression the moveHandlesFrom split exists for: this tab is standing
    // at the root, but the nodes were cut from folder 7, and folder 7 is what
    // the other tabs have to refresh.
    givenRootListing({});
    controller->loadRoot();
    flush();
    clipboard->cut(clipboardEntries({entry("a", 1)}), 7, false);

    quint64 reportedSource = 0;
    bool reportedSourceIsRoot = true;
    QObject::connect(controller.get(),
                     &FolderNavigationController::nodesMoved,
                     controller.get(),
                     [&](quint64, bool, quint64 source, bool sourceIsRoot) {
                         reportedSource = source;
                         reportedSourceIsRoot = sourceIsRoot;
                     });

    EXPECT_CALL(*client, checkMove(_, _, _)).WillRepeatedly(Return(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(_, _, _, _)).WillOnce(InvokeArgument<3>(Result<void>::ok()));

    controller->paste();
    flush();
    flush();

    EXPECT_EQ(reportedSource, 7u);
    EXPECT_FALSE(reportedSourceIsRoot);
}

TEST_F(FolderNavigationControllerTest, MoveHandlesToStillReportsItsOwnFolderAsTheSource)
{
    // The other half of that split: a drag's source is the dragging tab, which
    // here is the root.
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    bool reportedSourceIsRoot = false;
    QObject::connect(controller.get(),
                     &FolderNavigationController::nodesMoved,
                     controller.get(),
                     [&](quint64, bool, quint64, bool sourceIsRoot) {
                         reportedSourceIsRoot = sourceIsRoot;
                     });

    EXPECT_CALL(*client, checkMove(_, _, _)).WillRepeatedly(Return(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(_, _, _, _)).WillOnce(InvokeArgument<3>(Result<void>::ok()));

    controller->moveHandlesTo({1u}, 99, false);
    flush();

    EXPECT_TRUE(reportedSourceIsRoot);
}

TEST_F(FolderNavigationControllerTest, PasteEmitsNodesCopiedOnlyWhenSomethingSucceeded)
{
    givenRootListing({});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a", 1)}), 7, false);

    int copiedSignals = 0;
    QObject::connect(controller.get(),
                     &FolderNavigationController::nodesCopied,
                     controller.get(),
                     [&](quint64, bool) {
                         ++copiedSignals;
                     });

    EXPECT_CALL(*client, copyNode(_, _, _, _, _))
        .WillOnce(InvokeArgument<4>(Result<void>::fail("gone")))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));

    controller->paste();
    flush();
    flush();
    EXPECT_EQ(copiedSignals, 0);

    controller->paste();
    flush();
    flush();
    EXPECT_EQ(copiedSignals, 1);
}

TEST_F(FolderNavigationControllerTest, PasteClearsTheClipboardAfterACutButNotAfterACopy)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).WillOnce(InvokeArgument<4>(Result<void>::ok()));
    clipboard->copy(clipboardEntries({entry("a", 1)}), 7, false);
    controller->paste();
    flush();
    flush();
    // Pasting a copy twice is a legitimate way to get two copies.
    EXPECT_TRUE(clipboard->hasContent());

    EXPECT_CALL(*client, checkMove(_, _, _)).WillRepeatedly(Return(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(_, _, _, _)).WillOnce(InvokeArgument<3>(Result<void>::ok()));
    clipboard->cut(clipboardEntries({entry("a", 1)}), 7, false);
    controller->paste();
    flush();
    flush();
    EXPECT_FALSE(clipboard->hasContent());
}

TEST_F(FolderNavigationControllerTest, PasteReportsAnErrorWithoutCopyingWhenTheDestinationRefuses)
{
    givenRootListing({});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a", 1)}), 7, false);

    EXPECT_CALL(*client, checkUpload(_, _))
        .WillRepeatedly(Return(Result<void>::fail("read-only share", MegaErrorCode::kEAccess)));
    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).Times(0);

    controller->paste();
    flush();

    EXPECT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("paste"));
    EXPECT_EQ(operationCalls, 0);
}

TEST_F(FolderNavigationControllerTest, PasteDoesNothingWhenACutGoesBackIntoItsSourceFolder)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();
    // Cut at the root, pasted at the root: checkMove would refuse every node
    // with kEArgs, so this must not reach the SDK at all.
    clipboard->cut(clipboardEntries({entry("a", 1)}), 0, true);

    EXPECT_CALL(*client, moveNode(_, _, _, _)).Times(0);

    EXPECT_FALSE(controller->canPaste());
    controller->paste();
    flush();

    EXPECT_EQ(operationCalls, 0);
    EXPECT_EQ(errorCalls, 0);
}

TEST_F(FolderNavigationControllerTest, CanPasteIsFalseBeforeTheFirstLoad)
{
    clipboard->copy(clipboardEntries({entry("a", 1)}), 7, false);
    EXPECT_FALSE(controller->canPaste());
}

// --- Ctrl+drag copy (Phase 23a) -------------------------------------------
// copyEntriesTo is paste()'s copy branch with the destination passed in rather
// than read off this tab, so the interesting assertions are about *which*
// folder's names the auto-rename is chosen against.

TEST_F(FolderNavigationControllerTest, CanCopyEntriesOnRefusesAnEmptyDrag)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_FALSE(controller->canCopyEntriesOn(QVariantList{}, 7, false));
}

TEST_F(FolderNavigationControllerTest, CanCopyEntriesOnRefusesWhenTheDestinationTakesNoChildren)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, checkUpload(7u, false))
        .WillRepeatedly(Return(Result<void>::fail("read-only", MegaErrorCode::kEAccess)));

    EXPECT_FALSE(controller->canCopyEntriesOn(clipboardEntries({entry("a", 1)}), 7, false));
}

TEST_F(FolderNavigationControllerTest, CanCopyEntriesOnRefusesAFolderIntoItsOwnSubtree)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    // checkMove is what reports circularity; canCopy passes it straight through.
    EXPECT_CALL(*client, checkMove(1u, 7u, false))
        .WillRepeatedly(Return(Result<void>::fail("circular", MegaErrorCode::kECircular)));

    EXPECT_FALSE(controller->canCopyEntriesOn(clipboardEntries({entry("a", 1, true)}), 7, false));
}

TEST_F(FolderNavigationControllerTest, CanCopyEntriesOnAllowsTheFolderTheNodesAlreadyLiveIn)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, checkMove(1u, 7u, false))
        .WillRepeatedly(
            Return(Result<void>::fail("Already in that folder", MegaErrorCode::kEArgs)));

    EXPECT_TRUE(controller->canCopyEntriesOn(clipboardEntries({entry("a", 1)}), 7, false));
}

TEST_F(FolderNavigationControllerTest, CopyEntriesToReadsTheDropTargetNotTheCurrentFolder)
{
    givenRootListing({entry("a.txt", 1)});
    controller->loadRoot();
    flush();

    // The tab is showing the root; the drop landed on folder 7.
    bool sevenRead = false;
    EXPECT_CALL(*client, getChildren(7u, _, _))
        .WillOnce(Invoke([&](std::uint64_t,
                             SortOrder,
                             std::function<void(Result<std::vector<FileEntry>>)> onDone) {
            sevenRead = true;
            onDone(Result<std::vector<FileEntry>>::ok({}));
        }));
    EXPECT_CALL(*client, copyNode(1u, 7u, false, std::string(), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));

    controller->copyEntriesTo(clipboardEntries({entry("a.txt", 1)}), 7, false);
    flush();
    flush();

    EXPECT_TRUE(sevenRead);
}

TEST_F(FolderNavigationControllerTest, CopyEntriesToAutoRenamesAgainstTheDropTargetsNames)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, getChildren(7u, _, _))
        .WillOnce(InvokeArgument<2>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{entry("a.txt", 90)})));
    EXPECT_CALL(*client, copyNode(1u, 7u, false, std::string("a - Copy.txt"), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));

    controller->copyEntriesTo(clipboardEntries({entry("a.txt", 1)}), 7, false);
    flush();
    flush();

    EXPECT_EQ(operationCalls, 1);
}

TEST_F(FolderNavigationControllerTest, CopyEntriesToRefusesTheWholeDropWhenTheTargetCantBeRead)
{
    givenRootListing({entry("a.txt", 1)});
    controller->loadRoot();
    flush();

    // Unlike paste(), there is no cached listing of folder 7 to fall back on --
    // and falling back to this tab's would pick names against the wrong folder,
    // which is what silently versions over an existing file.
    EXPECT_CALL(*client, getChildren(7u, _, _))
        .WillOnce(InvokeArgument<2>(
            Result<std::vector<FileEntry>>::fail("gone", MegaErrorCode::kENoEnt)));
    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).Times(0);

    controller->copyEntriesTo(clipboardEntries({entry("a.txt", 1)}), 7, false);
    flush();
    flush();

    EXPECT_EQ(operationCalls, 0);
    EXPECT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("copy"));
}

TEST_F(FolderNavigationControllerTest,
       CopyEntriesToReportsAnErrorWithoutReadingWhenTheTargetRefuses)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, checkUpload(7u, false))
        .WillRepeatedly(Return(Result<void>::fail("read-only", MegaErrorCode::kEAccess)));
    EXPECT_CALL(*client, getChildren(7u, _, _)).Times(0);
    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).Times(0);

    controller->copyEntriesTo(clipboardEntries({entry("a.txt", 1)}), 7, false);
    flush();

    EXPECT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("copy"));
}

TEST_F(FolderNavigationControllerTest, CopyEntriesToEmitsNodesCopiedForTheDropTarget)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, getChildren(7u, _, _))
        .WillOnce(InvokeArgument<2>(Result<std::vector<FileEntry>>::ok({})));
    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).WillOnce(InvokeArgument<4>(Result<void>::ok()));

    quint64 destination = 0;
    bool destinationIsRoot = true;
    QObject::connect(controller.get(),
                     &FolderNavigationController::nodesCopied,
                     controller.get(),
                     [&](quint64 handle, bool isRoot) {
                         destination = handle;
                         destinationIsRoot = isRoot;
                     });

    controller->copyEntriesTo(clipboardEntries({entry("a.txt", 1)}), 7, false);
    flush();
    flush();

    EXPECT_EQ(destination, 7u);
    EXPECT_FALSE(destinationIsRoot);
}

TEST_F(FolderNavigationControllerTest, CopyEntriesToDoesNotRefetchAListingItDidNotChange)
{
    givenRootListing({entry("a.txt", 1)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, getChildren(7u, _, _))
        .WillOnce(InvokeArgument<2>(Result<std::vector<FileEntry>>::ok({})));
    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).WillOnce(InvokeArgument<4>(Result<void>::ok()));
    const int fetchesBefore = rootFetches;

    controller->copyEntriesTo(clipboardEntries({entry("a.txt", 1)}), 7, false);
    flush();
    flush();

    // A copy leaves the source folder alone, and this tab is showing the root,
    // not folder 7 -- so nothing here needs re-reading. Other tabs are reached
    // through nodesCopied.
    EXPECT_EQ(rootFetches - fetchesBefore, 0);
}

TEST_F(FolderNavigationControllerTest, CanPasteIsFalseForACopyOfAFolderIntoItsOwnSubtree)
{
    givenRootListing({});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a", 1, true)}), 7, false);

    EXPECT_CALL(*client, checkMove(1u, _, true))
        .WillRepeatedly(Return(Result<void>::fail("circular", MegaErrorCode::kECircular)));
    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).Times(0);

    EXPECT_FALSE(controller->canPaste());
    controller->paste();
    flush();
    flush();

    EXPECT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("paste"));
}
