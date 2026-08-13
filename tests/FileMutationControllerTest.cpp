#include "qml/FileMutationController.h"

#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"
#include "qml/BusyState.h"
#include "qml/ClipboardController.h"
#include "qml/FolderNavigationController.h"
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

// BusyState only publishes itself once its delay timer has fired, so the busy
// tests have to let real time pass rather than just drain the queue. Waits on
// the shared BusyState rather than the navigation controller's busy property:
// this file's subject doesn't publish one (see FileMutationController.h).
// Deliberately not QSignalSpy: that lives in Qt6::Test, which this target
// doesn't link (see UploadControllerTest's note on the same choice). Same shape
// as BusyStateTest's and BulkOperationRunnerTest's own copies.
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

class FileMutationControllerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        client = std::make_shared<MockMegaClient>();
        navigationService = std::make_shared<FolderNavigationService>(client);
        searchService = std::make_shared<SearchService>(client, navigationService);
        fileOps = std::make_shared<FileOperationService>(client);
        notifications = std::make_unique<NotificationController>();
        clipboard = std::make_unique<ClipboardController>();
        // makeGuiOwned like main.cpp -- see TabsControllerTest's note.
        busy = makeGuiOwned<BusyState>();
        // The real navigation controller, not a fake: most assertions below
        // count getRootChildren calls, i.e. that the mutation actually caused a
        // re-read that reached the SDK and landed in the model. A fake could
        // only report that some refresh hook was called.
        controller = makeGuiOwned<FolderNavigationController>(
            navigationService, searchService, busy, notifications.get());
        mutations = makeGuiOwned<FileMutationController>(
            controller, navigationService, fileOps, busy, notifications.get(), clipboard.get());

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

        QObject::connect(mutations.get(),
                         &FileMutationController::copyNameConflict,
                         mutations.get(),
                         [this](QVariantList entries,
                                QStringList names,
                                quint64 destination,
                                bool destinationIsRoot) {
                             ++conflictCalls;
                             lastConflictEntries = entries;
                             lastConflictNames = names;
                             lastConflictDestination = destination;
                             lastConflictDestinationIsRoot = destinationIsRoot;
                         });

        // Result<void>::success defaults to
        // false, so an unstubbed paste pre-check *refuses* rather than merely
        // doing nothing. A test that wants it to fail sets its own EXPECT_CALL.
        EXPECT_CALL(*client, checkUpload(_, _)).WillRepeatedly(Return(Result<void>::ok()));
        // And again for the per-source half: FileOperationService::copy() gates
        // on canCopy(), which is checkMove reinterpreted, so an unstubbed
        // checkMove refuses every copy before it reaches copyNode.
        EXPECT_CALL(*client, checkMove(_, _, _)).WillRepeatedly(Return(Result<void>::ok()));
    }

    // What ConfirmRubbishDialog.qml passes: the selection sampled as bare
    // handles at the moment the gesture is confirmed.
    QVariantList selectedHandles()
    {
        return model()->selectedHandlesVariant();
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
        return controller->fileListModelForThumbnails().get();
    }

    std::shared_ptr<MockMegaClient> client;
    std::shared_ptr<FolderNavigationService> navigationService;
    std::shared_ptr<SearchService> searchService;
    std::shared_ptr<FileOperationService> fileOps;
    std::unique_ptr<NotificationController> notifications;
    std::unique_ptr<ClipboardController> clipboard;
    std::shared_ptr<BusyState> busy;
    std::shared_ptr<FolderNavigationController> controller;
    std::shared_ptr<FileMutationController> mutations;

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
    int conflictCalls = 0;
    QVariantList lastConflictEntries;
    QStringList lastConflictNames;
    quint64 lastConflictDestination = 0;
    bool lastConflictDestinationIsRoot = false;
};

} // namespace

TEST_F(FileMutationControllerTest, MoveHandlesToRubbishReportsOneTallyForTheWholeSelection)
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

    mutations->moveHandlesToRubbish(selectedHandles());
    flush();

    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastContext, QStringLiteral("moveToRubbish"));
    EXPECT_EQ(lastSucceeded, 3);
    EXPECT_EQ(lastFailed, 0);
    // One refetch for the batch, not one per item.
    EXPECT_EQ(rootFetches - fetchesBefore, 1);
}

TEST_F(FileMutationControllerTest, MoveHandlesToRubbishSeparatesSucceededFromFailed)
{
    givenRootListing({entry("a", 1), entry("b", 2), entry("c", 3)});
    controller->loadRoot();
    flush();
    model()->selectAll();

    EXPECT_CALL(*client, moveToRubbish(1u, _)).WillOnce(InvokeArgument<1>(Result<void>::ok()));
    EXPECT_CALL(*client, moveToRubbish(2u, _))
        .WillOnce(InvokeArgument<1>(Result<void>::fail("access denied", MegaErrorCode::kEAccess)));
    EXPECT_CALL(*client, moveToRubbish(3u, _)).WillOnce(InvokeArgument<1>(Result<void>::ok()));
    const int fetchesBefore = rootFetches;

    mutations->moveHandlesToRubbish(selectedHandles());
    flush();

    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastSucceeded, 2);
    EXPECT_EQ(lastFailed, 1);
    // A partial failure still refreshes -- the ones that worked are gone.
    EXPECT_EQ(rootFetches - fetchesBefore, 1);
}

TEST_F(FileMutationControllerTest, MoveHandlesToRubbishHandlesASingleItem)
{
    givenRootListing({entry("a", 1), entry("b", 2)});
    controller->loadRoot();
    flush();
    model()->selectRow(1, Qt::NoModifier);

    EXPECT_CALL(*client, moveToRubbish(2u, _)).WillOnce(InvokeArgument<1>(Result<void>::ok()));
    const int fetchesBefore = rootFetches;

    mutations->moveHandlesToRubbish(selectedHandles());
    flush();

    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastSucceeded, 1);
    EXPECT_EQ(lastFailed, 0);
    EXPECT_EQ(rootFetches - fetchesBefore, 1);
}

TEST_F(FileMutationControllerTest, MoveHandlesToRubbishDoesNothingWithAnEmptySelection)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();
    model()->clearSelection();

    EXPECT_CALL(*client, moveToRubbish(_, _)).Times(0);
    const int fetchesBefore = rootFetches;

    mutations->moveHandlesToRubbish(selectedHandles());
    flush();

    EXPECT_EQ(operationCalls, 0);
    EXPECT_EQ(rootFetches - fetchesBefore, 0);
}

TEST_F(FileMutationControllerTest, RestoreSendsEachNodeToItsRecordedFolder)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, getRestoreTarget(5u))
        .WillRepeatedly(Return(Result<RestoreTarget>::ok(RestoreTarget{40, false, false})));
    EXPECT_CALL(*client, getRestoreTarget(6u))
        .WillRepeatedly(Return(Result<RestoreTarget>::ok(RestoreTarget{41, false, false})));
    EXPECT_CALL(*client, moveNode(5u, 40u, false, _))
        .WillOnce(InvokeArgument<3>(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(6u, 41u, false, _))
        .WillOnce(InvokeArgument<3>(Result<void>::ok()));

    mutations->restoreHandles(QVariantList{QVariant(quint64(5)), QVariant(quint64(6))});
    flush();

    EXPECT_EQ(lastContext, QStringLiteral("restore"));
    EXPECT_EQ(lastSucceeded, 2);
    EXPECT_EQ(lastFailed, 0);
}

TEST_F(FileMutationControllerTest, RestoreFallsBackToTheRootAndSaysSoWhenTheFolderIsGone)
{
    // The wording is the whole point of carrying fellBackToRoot up: the node lands
    // somewhere other than where it was binned from, and silently is the one way
    // that must not happen.
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, getRestoreTarget(5u))
        .WillRepeatedly(Return(Result<RestoreTarget>::ok(RestoreTarget{0, true, true})));
    EXPECT_CALL(*client, moveNode(5u, 0u, true, _)).WillOnce(InvokeArgument<3>(Result<void>::ok()));

    mutations->restoreHandles(QVariantList{QVariant(quint64(5))});
    flush();

    EXPECT_EQ(lastContext, QStringLiteral("restoreToRoot"));
    EXPECT_EQ(lastSucceeded, 1);
}

TEST_F(FileMutationControllerTest, RestoreCountsAnUnresolvableNodeAsFailedWithoutLosingTheTally)
{
    // A handle that no longer resolves never reaches the SDK, so it would drop out
    // of the batch entirely and the tally would report fewer items than the user
    // selected.
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, getRestoreTarget(5u))
        .WillRepeatedly(Return(Result<RestoreTarget>::ok(RestoreTarget{40, false, false})));
    EXPECT_CALL(*client, getRestoreTarget(9u))
        .WillRepeatedly(Return(Result<RestoreTarget>::fail("gone", MegaErrorCode::kENoEnt)));
    EXPECT_CALL(*client, moveNode(5u, 40u, false, _))
        .WillOnce(InvokeArgument<3>(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(9u, _, _, _)).Times(0);

    mutations->restoreHandles(QVariantList{QVariant(quint64(5)), QVariant(quint64(9))});
    flush();

    EXPECT_EQ(lastSucceeded, 1);
    EXPECT_EQ(lastFailed, 1);
}

TEST_F(FileMutationControllerTest, RestoreReportsWithoutCallingTheSdkWhenNothingResolves)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, getRestoreTarget(_))
        .WillRepeatedly(Return(Result<RestoreTarget>::fail("gone", MegaErrorCode::kENoEnt)));
    EXPECT_CALL(*client, moveNode(_, _, _, _)).Times(0);

    mutations->restoreHandles(QVariantList{QVariant(quint64(9))});
    flush();

    EXPECT_EQ(lastSucceeded, 0);
    EXPECT_EQ(lastFailed, 1);
}

TEST_F(FileMutationControllerTest, DeletePermanentlyRemovesEveryHandleAndReportsOneTally)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, removeNode(5u, _)).WillOnce(InvokeArgument<1>(Result<void>::ok()));
    EXPECT_CALL(*client, removeNode(6u, _))
        .WillOnce(InvokeArgument<1>(Result<void>::fail("nope", MegaErrorCode::kEAccess)));

    mutations->deleteHandlesPermanently(QVariantList{QVariant(quint64(5)), QVariant(quint64(6))});
    flush();

    EXPECT_EQ(lastContext, QStringLiteral("deletePermanently"));
    EXPECT_EQ(operationCalls, 1);
    EXPECT_EQ(lastSucceeded, 1);
    EXPECT_EQ(lastFailed, 1);
}

TEST_F(FileMutationControllerTest, DeletePermanentlyDoesNothingWithAnEmptySelection)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();
    model()->clearSelection();

    EXPECT_CALL(*client, removeNode(_, _)).Times(0);

    mutations->deleteHandlesPermanently(selectedHandles());
    flush();

    EXPECT_EQ(operationCalls, 0);
}

TEST_F(FileMutationControllerTest, EmptyRubbishBinIssuesOneRequestForTheWholeBin)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, cleanRubbishBin(_)).WillOnce(InvokeArgument<0>(Result<void>::ok()));

    mutations->emptyRubbishBin();
    flush();

    EXPECT_EQ(lastContext, QStringLiteral("emptyRubbish"));
    EXPECT_EQ(lastSucceeded, 1);
    EXPECT_EQ(lastFailed, 0);
}

TEST_F(FileMutationControllerTest, EmptyRubbishBinLeavesATabThatIsNotShowingTheBinAlone)
{
    // The action is reachable from the side panel while any screen is open, so this
    // tab is on the Cloud Drive here -- re-reading it would be a request for a
    // listing the emptying cannot have changed.
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, cleanRubbishBin(_)).WillOnce(InvokeArgument<0>(Result<void>::ok()));
    const int fetchesBefore = rootFetches;

    mutations->emptyRubbishBin();
    flush();

    EXPECT_EQ(lastSucceeded, 1);
    EXPECT_EQ(rootFetches - fetchesBefore, 0);
}

TEST_F(FileMutationControllerTest, MoveHandlesToReportsOneTallyForTheWholeDrop)
{
    givenRootListing({entry("a", 1), entry("b", 2), entry("c", 3)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, checkMove(_, 99u, false)).WillRepeatedly(Return(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(_, 99u, false, _))
        .Times(3)
        .WillRepeatedly(InvokeArgument<3>(Result<void>::ok()));
    const int fetchesBefore = rootFetches;

    mutations->moveHandlesTo({1u, 2u, 3u}, 99, false);
    flush();

    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastContext, QStringLiteral("move"));
    EXPECT_EQ(lastSucceeded, 3);
    EXPECT_EQ(lastFailed, 0);
    // One refetch for the whole drop, not one per item.
    EXPECT_EQ(rootFetches - fetchesBefore, 1);
}

TEST_F(FileMutationControllerTest, MoveHandlesToSeparatesSucceededFromFailed)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, checkMove(_, _, _)).WillRepeatedly(Return(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(1u, _, _, _)).WillOnce(InvokeArgument<3>(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(2u, _, _, _))
        .WillOnce(InvokeArgument<3>(Result<void>::fail("access denied", MegaErrorCode::kEAccess)));

    mutations->moveHandlesTo({1u, 2u}, 99, false);
    flush();

    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastSucceeded, 1);
    EXPECT_EQ(lastFailed, 1);
}

TEST_F(FileMutationControllerTest, MoveHandlesToForwardsTheRootSentinel)
{
    // Dropping onto the tree's "Cloud Drive" row: the handle is meaningless and
    // only the isRoot flag identifies the destination.
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, checkMove(1u, _, true)).WillOnce(Return(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(1u, _, true, _)).WillOnce(InvokeArgument<3>(Result<void>::ok()));

    mutations->moveHandlesTo({1u}, 0, true);
    flush();

    EXPECT_EQ(lastSucceeded, 1);
}

TEST_F(FileMutationControllerTest, MoveHandlesToDoesNothingWithAnEmptyDrop)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, moveNode(_, _, _, _)).Times(0);
    const int fetchesBefore = rootFetches;

    mutations->moveHandlesTo({}, 99, false);
    flush();

    EXPECT_EQ(operationCalls, 0);
    EXPECT_EQ(rootFetches - fetchesBefore, 0);
}

TEST_F(FileMutationControllerTest, CanDropHandlesOnRejectsUnlessEveryHandlePasses)
{
    // A drop target is all-or-nothing: one un-movable item greys out the whole
    // drop rather than silently moving the rest.
    EXPECT_CALL(*client, checkMove(1u, 99u, false)).WillRepeatedly(Return(Result<void>::ok()));
    EXPECT_CALL(*client, checkMove(2u, 99u, false))
        .WillRepeatedly(Return(Result<void>::fail("circular", MegaErrorCode::kECircular)));

    EXPECT_TRUE(mutations->canDropHandlesOn({1u}, 99, false));
    EXPECT_FALSE(mutations->canDropHandlesOn({1u, 2u}, 99, false));
}

TEST_F(FileMutationControllerTest, CanDropHandlesOnRejectsAnEmptyDrag)
{
    EXPECT_CALL(*client, checkMove(_, _, _)).Times(0);

    EXPECT_FALSE(mutations->canDropHandlesOn({}, 99, false));
}

TEST_F(FileMutationControllerTest, RenameEntryReportsAnInvalidNameWithoutRefetching)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    // Rejected by FileOperationService's own validation, so the SDK is never
    // reached and the listing is left alone.
    EXPECT_CALL(*client, renameNode(_, _, _)).Times(0);
    const int fetchesBefore = rootFetches;

    mutations->renameEntry(1, QStringLiteral("bad/name"));
    flush();

    EXPECT_EQ(errorCalls, 1);
    // Its own context, not "rename": a name the user can retype is an input
    // mistake, not a failed operation (R3-6).
    EXPECT_EQ(lastErrorContext, QStringLiteral("renameInvalidName"));
    EXPECT_EQ(rootFetches - fetchesBefore, 0);
}

TEST_F(FileMutationControllerTest, RenameEntryReportsARealFailureAsAnOperationFailure)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    // Anything that isn't kEArgs stays on the generic path -- guards against
    // the invalid-name branch swallowing every failure.
    EXPECT_CALL(*client, renameNode(1u, std::string("b"), _))
        .WillOnce(InvokeArgument<2>(Result<void>::fail("denied", MegaErrorCode::kEAccess)));
    const int fetchesBefore = rootFetches;

    mutations->renameEntry(1, QStringLiteral("b"));
    flush();

    EXPECT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("rename"));
    EXPECT_EQ(lastErrorReason, NotificationController::NoPermission);
    // "denied" is the SDK's English; a classified reason must not carry it.
    EXPECT_TRUE(lastErrorRaw.isEmpty());
    EXPECT_EQ(rootFetches - fetchesBefore, 0);
}

TEST_F(FileMutationControllerTest, RenameEntryRefetchesOnSuccess)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, renameNode(1u, std::string("b"), _))
        .WillOnce(InvokeArgument<2>(Result<void>::ok()));
    const int fetchesBefore = rootFetches;

    mutations->renameEntry(1, QStringLiteral("b"));
    flush();

    EXPECT_EQ(errorCalls, 0);
    EXPECT_EQ(rootFetches - fetchesBefore, 1);
}

TEST_F(FileMutationControllerTest, SetEntryFavouriteUpdatesTheRowInPlaceWithoutRefetching)
{
    givenRootListing({entry("a", 1), entry("b", 2)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, setNodeFavourite(2u, true, _))
        .WillOnce(InvokeArgument<2>(Result<void>::ok()));
    const int fetchesBefore = rootFetches;

    mutations->setEntryFavourite(2, true);
    flush();

    EXPECT_EQ(errorCalls, 0);
    // No toast on success either -- a heart is toggled often enough that one
    // would be noise.
    EXPECT_EQ(operationCalls, 0);
    // The point of the whole in-place path: a refetch would reset the model.
    EXPECT_EQ(rootFetches - fetchesBefore, 0);
    EXPECT_TRUE(model()->data(model()->index(1, 0), FileListModel::IsFavouriteRole).toBool());
}

TEST_F(FileMutationControllerTest, SetEntryFavouriteAnnouncesItselfOnlyOnSuccess)
{
    // The other tabs learn about the flag from this signal alone -- a favourites
    // listing elsewhere would otherwise never notice (spec 5.3). Reporting an
    // attempt that failed would make those tabs show a state the account is not in.
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    int reported = 0;
    quint64 reportedHandle = 0;
    bool reportedValue = false;
    QObject::connect(mutations.get(),
                     &FileMutationController::favouriteChanged,
                     mutations.get(),
                     [&](quint64 handle, bool favourite) {
                         ++reported;
                         reportedHandle = handle;
                         reportedValue = favourite;
                     });

    EXPECT_CALL(*client, setNodeFavourite(1u, true, _))
        .WillOnce(InvokeArgument<2>(Result<void>::ok()));
    mutations->setEntryFavourite(1, true);
    flush();

    ASSERT_EQ(reported, 1);
    EXPECT_EQ(reportedHandle, 1u);
    EXPECT_TRUE(reportedValue);

    EXPECT_CALL(*client, setNodeFavourite(1u, false, _))
        .WillOnce(InvokeArgument<2>(Result<void>::fail("denied", MegaErrorCode::kEAccess)));
    mutations->setEntryFavourite(1, false);
    flush();

    EXPECT_EQ(reported, 1);
}

TEST_F(FileMutationControllerTest, SetEntryFavouriteReportsFailureUnderTheActionsOwnContext)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, setNodeFavourite(1u, false, _))
        .WillOnce(InvokeArgument<2>(Result<void>::fail("denied", MegaErrorCode::kEAccess)));

    mutations->setEntryFavourite(1, false);
    flush();

    ASSERT_EQ(errorCalls, 1);
    // Not a shared "favourite" context: ToastStack.qml words the two directions
    // differently.
    EXPECT_EQ(lastErrorContext, QStringLiteral("removeFavourite"));
    EXPECT_EQ(lastErrorReason, NotificationController::NoPermission);
    // The model keeps the server's state, not the one the user asked for.
    EXPECT_FALSE(model()->data(model()->index(0, 0), FileListModel::IsFavouriteRole).toBool());
}

TEST_F(FileMutationControllerTest, SetEntryFavouriteAppliesTheRequestedValueEvenIfItAlreadyHoldsIt)
{
    // The drift case: the listing says "not a favourite" but the server
    // disagrees. setNodeFavourite is idempotent, so honouring the user's intent
    // needs no read-back.
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, setNodeFavourite(1u, true, _))
        .Times(2)
        .WillRepeatedly(InvokeArgument<2>(Result<void>::ok()));

    mutations->setEntryFavourite(1, true);
    flush();
    mutations->setEntryFavourite(1, true);
    flush();

    EXPECT_EQ(errorCalls, 0);
    EXPECT_TRUE(model()->data(model()->index(0, 0), FileListModel::IsFavouriteRole).toBool());
}

TEST_F(FileMutationControllerTest, BusyClearsOnlyAfterTheLastCallbackOfABulkOperation)
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

    mutations->moveHandlesToRubbish(selectedHandles());
    ASSERT_EQ(pending.size(), 2u);
    ASSERT_TRUE(waitForBusy(*busy, true));

    pending[0](Result<void>::ok());
    flush();
    EXPECT_TRUE(controller->busy());

    pending[1](Result<void>::ok());
    flush();
    EXPECT_FALSE(controller->busy());
}

TEST_F(FileMutationControllerTest, BusyClearsWhenAnOperationFails)
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

    mutations->createFolder(QStringLiteral("x"));
    ASSERT_TRUE(pending);
    ASSERT_TRUE(waitForBusy(*busy, true));

    // The kEExist branch returns early and without a toast -- of createFolder's
    // four outcomes, the one most likely to leak the count if it were
    // decremented per-branch rather than once above them.
    pending(Result<void>::fail("already exists", MegaErrorCode::kEExist));
    flush();

    EXPECT_FALSE(controller->busy());
}

TEST_F(FileMutationControllerTest, FolderNameTakenSeesFoldersOfTheCachedListing)
{
    givenRootListing({entry("docs", 1, true), entry("notes.txt", 2)});
    controller->loadRoot();
    flush();

    EXPECT_TRUE(mutations->folderNameTaken(QStringLiteral("docs")));
    EXPECT_FALSE(mutations->folderNameTaken(QStringLiteral("photos")));
}

TEST_F(FileMutationControllerTest, FolderNameTakenIgnoresFilesAndIsCaseSensitive)
{
    givenRootListing({entry("notes.txt", 1), entry("Docs", 2, true)});
    controller->loadRoot();
    flush();

    // A file of that name is no conflict for createFolder, and MEGA's own check
    // is byte-wise -- warning about either would be a warning the server
    // contradicts.
    EXPECT_FALSE(mutations->folderNameTaken(QStringLiteral("notes.txt")));
    EXPECT_FALSE(mutations->folderNameTaken(QStringLiteral("docs")));
}

TEST_F(FileMutationControllerTest, FolderNameTakenIsFalseBeforeTheFirstLoad)
{
    EXPECT_FALSE(mutations->folderNameTaken(QStringLiteral("docs")));
}

TEST_F(FileMutationControllerTest, ResetClearsBusyWithOperationsStillInFlight)
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

    mutations->moveHandlesToRubbish(selectedHandles());
    ASSERT_TRUE(waitForBusy(*busy, true));

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

    mutations->moveHandlesToRubbish(selectedHandles());
    EXPECT_TRUE(waitForBusy(*busy, true));
}

// Paste (Phase 23) is the same bookkeeping concern as the fan-outs above, plus
// one of its own: the destination's names have to be read *before* any copy
// goes out, since a colliding name silently versions over the existing file
// instead of landing beside it (IMegaClient::copyNode).

TEST_F(FileMutationControllerTest, PasteDoesNothingWithAnEmptyClipboard)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).Times(0);
    EXPECT_CALL(*client, moveNode(_, _, _, _)).Times(0);
    const int fetchesBefore = rootFetches;

    mutations->paste();
    flush();

    EXPECT_EQ(operationCalls, 0);
    EXPECT_EQ(rootFetches - fetchesBefore, 0);
}

TEST_F(FileMutationControllerTest, PasteReadsTheDestinationBeforeCopying)
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

    mutations->paste();
    flush();
    flush();

    EXPECT_EQ(fetchesWhenCopied, fetchesBefore + 1);
}

TEST_F(FileMutationControllerTest, PasteCopiesEveryClipboardEntryAndReportsOneTally)
{
    givenRootListing({});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a", 1), entry("b", 2), entry("c", 3)}), 7, false);

    EXPECT_CALL(*client, copyNode(_, _, _, _, _))
        .Times(3)
        .WillRepeatedly(InvokeArgument<4>(Result<void>::ok()));

    mutations->paste();
    flush();
    flush();

    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastContext, QStringLiteral("copy"));
    EXPECT_EQ(lastSucceeded, 3);
    EXPECT_EQ(lastFailed, 0);
}

TEST_F(FileMutationControllerTest, PasteSeparatesSucceededFromFailedCopies)
{
    givenRootListing({});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a", 1), entry("b", 2)}), 7, false);

    EXPECT_CALL(*client, copyNode(1u, _, _, _, _)).WillOnce(InvokeArgument<4>(Result<void>::ok()));
    EXPECT_CALL(*client, copyNode(2u, _, _, _, _))
        .WillOnce(InvokeArgument<4>(Result<void>::fail("gone", MegaErrorCode::kENoEnt)));

    mutations->paste();
    flush();
    flush();

    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastSucceeded, 1);
    EXPECT_EQ(lastFailed, 1);
}

TEST_F(FileMutationControllerTest, PasteKeepsANonCollidingNameUnchanged)
{
    givenRootListing({entry("other.txt", 1)});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a.txt", 5)}), 7, false);

    // Empty name == "keep the source's", the only way to reach copyNode's
    // unnamed SDK overload.
    EXPECT_CALL(*client, copyNode(5u, _, true, std::string(), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));

    mutations->paste();
    flush();
    flush();

    EXPECT_EQ(lastSucceeded, 1);
}

// A file whose name the destination already holds stops the paste at the
// question below instead of being renamed on the spot, which is what it used to
// do unconditionally. Folders never reach it: two same-named folders coexist on
// MEGA, so there is nothing there to overwrite.

TEST_F(FileMutationControllerTest, PasteAsksBeforeCopyingOntoAnExistingFile)
{
    givenRootListing({entry("a.txt", 1)});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a.txt", 5), entry("b.txt", 6)}), 7, false);

    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).Times(0);

    mutations->paste();
    flush();
    flush();

    ASSERT_EQ(conflictCalls, 1);
    EXPECT_EQ(lastConflictNames, QStringList{QStringLiteral("a.txt")});
    // The whole batch comes back out, not just the colliding part: the answer
    // decides what happens to every entry.
    EXPECT_EQ(lastConflictEntries.size(), 2);
    EXPECT_TRUE(lastConflictDestinationIsRoot);
    EXPECT_EQ(operationCalls, 0);
}

TEST_F(FileMutationControllerTest, PasteDoesNotAskAboutAFolderWhoseNameIsTaken)
{
    givenRootListing({entry("shared", 1, true)});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("shared", 5, true)}), 7, false);

    EXPECT_CALL(*client, copyNode(5u, _, _, std::string("shared - Copy"), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));

    mutations->paste();
    flush();
    flush();

    EXPECT_EQ(conflictCalls, 0);
    EXPECT_EQ(lastSucceeded, 1);
}

TEST_F(FileMutationControllerTest, PasteDoesNotAskWhenOnlyAFolderHoldsTheName)
{
    // A folder of that name can't be versioned over, so there is nothing to ask
    // about -- but the new name still has to dodge it, or the copy lands as an
    // untidy same-named sibling.
    givenRootListing({entry("Reports", 1, true)});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("Reports", 5)}), 7, false);

    EXPECT_CALL(*client, copyNode(5u, _, _, std::string("Reports - Copy"), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));

    mutations->paste();
    flush();
    flush();

    EXPECT_EQ(conflictCalls, 0);
    EXPECT_EQ(lastSucceeded, 1);
}

TEST_F(FileMutationControllerTest, CopyRenamingExistingAutoRenamesOnlyTheCollidingEntry)
{
    givenRootListing({entry("a.txt", 1)});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a.txt", 5), entry("b.txt", 6)}), 7, false);

    mutations->paste();
    flush();
    flush();
    ASSERT_EQ(conflictCalls, 1);

    EXPECT_CALL(*client, copyNode(5u, _, _, std::string("a - Copy.txt"), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));
    EXPECT_CALL(*client, copyNode(6u, _, _, std::string(), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));

    mutations->copyRenamingExisting(lastConflictEntries,
                                    lastConflictDestination,
                                    lastConflictDestinationIsRoot);
    flush();
    flush();

    EXPECT_EQ(lastSucceeded, 2);
}

TEST_F(FileMutationControllerTest, CopyReplacingExistingKeepsTheCollidingSourceName)
{
    // Empty name == "keep the source's", which is what makes the SDK attach the
    // copy as a new version over the existing file -- MEGA's nearest thing to an
    // overwrite (IMegaClient::copyNode).
    givenRootListing({entry("a.txt", 1)});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a.txt", 5), entry("b.txt", 6)}), 7, false);

    mutations->paste();
    flush();
    flush();
    ASSERT_EQ(conflictCalls, 1);

    EXPECT_CALL(*client, copyNode(5u, _, _, std::string(), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));
    EXPECT_CALL(*client, copyNode(6u, _, _, std::string(), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));

    mutations->copyReplacingExisting(lastConflictEntries,
                                     lastConflictDestination,
                                     lastConflictDestinationIsRoot);
    flush();
    flush();

    EXPECT_EQ(lastSucceeded, 2);
}

TEST_F(FileMutationControllerTest, CopySkippingExistingLeavesOutOnlyTheCollidingEntry)
{
    givenRootListing({entry("a.txt", 1)});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a.txt", 5), entry("b.txt", 6)}), 7, false);

    mutations->paste();
    flush();
    flush();
    ASSERT_EQ(conflictCalls, 1);

    EXPECT_CALL(*client, copyNode(5u, _, _, _, _)).Times(0);
    EXPECT_CALL(*client, copyNode(6u, _, _, std::string(), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));

    mutations->copySkippingExisting(lastConflictEntries,
                                    lastConflictDestination,
                                    lastConflictDestinationIsRoot);
    flush();
    flush();

    // One tally for one copy, not a batch of two with a phantom success.
    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastSucceeded, 1);
    EXPECT_EQ(lastFailed, 0);
}

TEST_F(FileMutationControllerTest, CopySkippingExistingIssuesNothingWhenEverythingCollides)
{
    givenRootListing({entry("a.txt", 1)});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a.txt", 5)}), 7, false);

    mutations->paste();
    flush();
    flush();
    ASSERT_EQ(conflictCalls, 1);

    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).Times(0);

    mutations->copySkippingExisting(lastConflictEntries,
                                    lastConflictDestination,
                                    lastConflictDestinationIsRoot);
    flush();
    flush();

    EXPECT_EQ(operationCalls, 0);
}

TEST_F(FileMutationControllerTest, CopyAnswerRefusesWhenTheDestinationCannotBeReRead)
{
    givenRootListing({entry("a.txt", 1)});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a.txt", 5)}), 7, false);

    mutations->paste();
    flush();
    flush();
    ASSERT_EQ(conflictCalls, 1);

    // The answer is only meaningful against the listing it was asked about, so
    // unlike paste() there is no cached fallback here.
    EXPECT_CALL(*client, getRootChildren(_, _))
        .WillRepeatedly(InvokeArgument<1>(
            Result<std::vector<FileEntry>>::fail("offline", MegaErrorCode::kEAgain)));
    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).Times(0);

    mutations->copyReplacingExisting(lastConflictEntries,
                                     lastConflictDestination,
                                     lastConflictDestinationIsRoot);
    flush();
    flush();

    EXPECT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("copy"));
}

TEST_F(FileMutationControllerTest, CopyRenamingExistingDoesNotHandOutTheSameGeneratedNameTwice)
{
    // MEGA allows duplicate siblings, so two clipboard entries really can share
    // a name -- and neither may be given the name the other just claimed.
    givenRootListing({entry("a.txt", 1)});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a.txt", 5), entry("a.txt", 6)}), 7, false);

    mutations->paste();
    flush();
    flush();
    ASSERT_EQ(conflictCalls, 1);

    EXPECT_CALL(*client, copyNode(5u, _, _, std::string("a - Copy.txt"), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));
    EXPECT_CALL(*client, copyNode(6u, _, _, std::string("a - Copy (2).txt"), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));

    mutations->copyRenamingExisting(lastConflictEntries,
                                    lastConflictDestination,
                                    lastConflictDestinationIsRoot);
    flush();
    flush();

    EXPECT_EQ(lastSucceeded, 2);
}

TEST_F(FileMutationControllerTest, PasteFallsBackToTheCachedListingWhenTheDestinationReadFails)
{
    givenRootListing({entry("a.txt", 1)});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a.txt", 5)}), 7, false);

    // Matched ahead of givenRootListing's expectation, so every read from here
    // on fails -- the paste has to fall back to what it already had, which is
    // what lets it still spot the collision.
    EXPECT_CALL(*client, getRootChildren(_, _))
        .WillRepeatedly(InvokeArgument<1>(
            Result<std::vector<FileEntry>>::fail("offline", MegaErrorCode::kEAgain)));
    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).Times(0);

    mutations->paste();
    flush();
    flush();

    ASSERT_EQ(conflictCalls, 1);
    EXPECT_EQ(lastConflictNames, QStringList{QStringLiteral("a.txt")});
}

TEST_F(FileMutationControllerTest, PasteMovesInsteadOfCopyingWhenTheClipboardHoldsACut)
{
    givenRootListing({});
    controller->loadRoot();
    flush();
    clipboard->cut(clipboardEntries({entry("a", 1)}), 7, false);

    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).Times(0);
    EXPECT_CALL(*client, checkMove(1u, _, true)).WillOnce(Return(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(1u, _, true, _)).WillOnce(InvokeArgument<3>(Result<void>::ok()));

    mutations->paste();
    flush();
    flush();

    ASSERT_EQ(operationCalls, 1);
    EXPECT_EQ(lastContext, QStringLiteral("move"));
    EXPECT_EQ(lastSucceeded, 1);
}

TEST_F(FileMutationControllerTest, PasteReportsTheClipboardsSourceFolderInNodesMoved)
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
    QObject::connect(mutations.get(),
                     &FileMutationController::nodesMoved,
                     mutations.get(),
                     [&](quint64, bool, quint64 source, bool sourceIsRoot) {
                         reportedSource = source;
                         reportedSourceIsRoot = sourceIsRoot;
                     });

    EXPECT_CALL(*client, checkMove(_, _, _)).WillRepeatedly(Return(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(_, _, _, _)).WillOnce(InvokeArgument<3>(Result<void>::ok()));

    mutations->paste();
    flush();
    flush();

    EXPECT_EQ(reportedSource, 7u);
    EXPECT_FALSE(reportedSourceIsRoot);
}

TEST_F(FileMutationControllerTest, MoveHandlesToStillReportsItsOwnFolderAsTheSource)
{
    // The other half of that split: a drag's source is the dragging tab, which
    // here is the root.
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();

    bool reportedSourceIsRoot = false;
    QObject::connect(mutations.get(),
                     &FileMutationController::nodesMoved,
                     mutations.get(),
                     [&](quint64, bool, quint64, bool sourceIsRoot) {
                         reportedSourceIsRoot = sourceIsRoot;
                     });

    EXPECT_CALL(*client, checkMove(_, _, _)).WillRepeatedly(Return(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(_, _, _, _)).WillOnce(InvokeArgument<3>(Result<void>::ok()));

    mutations->moveHandlesTo({1u}, 99, false);
    flush();

    EXPECT_TRUE(reportedSourceIsRoot);
}

TEST_F(FileMutationControllerTest, PasteEmitsNodesCopiedOnlyWhenSomethingSucceeded)
{
    givenRootListing({});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a", 1)}), 7, false);

    int copiedSignals = 0;
    QObject::connect(
        mutations.get(), &FileMutationController::nodesCopied, mutations.get(), [&](quint64, bool) {
            ++copiedSignals;
        });

    EXPECT_CALL(*client, copyNode(_, _, _, _, _))
        .WillOnce(InvokeArgument<4>(Result<void>::fail("gone", MegaErrorCode::kENoEnt)))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));

    mutations->paste();
    flush();
    flush();
    EXPECT_EQ(copiedSignals, 0);

    mutations->paste();
    flush();
    flush();
    EXPECT_EQ(copiedSignals, 1);
}

TEST_F(FileMutationControllerTest, PasteClearsTheClipboardAfterACutButNotAfterACopy)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).WillOnce(InvokeArgument<4>(Result<void>::ok()));
    clipboard->copy(clipboardEntries({entry("a", 1)}), 7, false);
    mutations->paste();
    flush();
    flush();
    // Pasting a copy twice is a legitimate way to get two copies.
    EXPECT_TRUE(clipboard->hasContent());

    EXPECT_CALL(*client, checkMove(_, _, _)).WillRepeatedly(Return(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(_, _, _, _)).WillOnce(InvokeArgument<3>(Result<void>::ok()));
    clipboard->cut(clipboardEntries({entry("a", 1)}), 7, false);
    mutations->paste();
    flush();
    flush();
    EXPECT_FALSE(clipboard->hasContent());
}

TEST_F(FileMutationControllerTest, PasteReportsAnErrorWithoutCopyingWhenTheDestinationRefuses)
{
    givenRootListing({});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a", 1)}), 7, false);

    EXPECT_CALL(*client, checkUpload(_, _))
        .WillRepeatedly(Return(Result<void>::fail("read-only share", MegaErrorCode::kEAccess)));
    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).Times(0);

    mutations->paste();
    flush();

    EXPECT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("paste"));
    EXPECT_EQ(operationCalls, 0);
}

TEST_F(FileMutationControllerTest, PasteDoesNothingWhenACutGoesBackIntoItsSourceFolder)
{
    givenRootListing({entry("a", 1)});
    controller->loadRoot();
    flush();
    // Cut at the root, pasted at the root: checkMove would refuse every node
    // with kEArgs, so this must not reach the SDK at all.
    clipboard->cut(clipboardEntries({entry("a", 1)}), 0, true);

    EXPECT_CALL(*client, moveNode(_, _, _, _)).Times(0);

    EXPECT_FALSE(mutations->canPaste());
    mutations->paste();
    flush();

    EXPECT_EQ(operationCalls, 0);
    EXPECT_EQ(errorCalls, 0);
}

TEST_F(FileMutationControllerTest, CanPasteIsFalseBeforeTheFirstLoad)
{
    clipboard->copy(clipboardEntries({entry("a", 1)}), 7, false);
    EXPECT_FALSE(mutations->canPaste());
}

// --- Ctrl+drag copy (Phase 23a) -------------------------------------------
// copyEntriesTo is paste()'s copy branch with the destination passed in rather
// than read off this tab, so the interesting assertions are about *which*
// folder's names the auto-rename is chosen against.

TEST_F(FileMutationControllerTest, CanCopyEntriesOnRefusesAnEmptyDrag)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_FALSE(mutations->canCopyEntriesOn(QVariantList{}, 7, false));
}

TEST_F(FileMutationControllerTest, CanCopyEntriesOnRefusesWhenTheDestinationTakesNoChildren)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, checkUpload(7u, false))
        .WillRepeatedly(Return(Result<void>::fail("read-only", MegaErrorCode::kEAccess)));

    EXPECT_FALSE(mutations->canCopyEntriesOn(clipboardEntries({entry("a", 1)}), 7, false));
}

TEST_F(FileMutationControllerTest, CanCopyEntriesOnRefusesAFolderIntoItsOwnSubtree)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    // checkMove is what reports circularity; canCopy passes it straight through.
    EXPECT_CALL(*client, checkMove(1u, 7u, false))
        .WillRepeatedly(Return(Result<void>::fail("circular", MegaErrorCode::kECircular)));

    EXPECT_FALSE(mutations->canCopyEntriesOn(clipboardEntries({entry("a", 1, true)}), 7, false));
}

TEST_F(FileMutationControllerTest, CanCopyEntriesOnAllowsTheFolderTheNodesAlreadyLiveIn)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, checkMove(1u, 7u, false))
        .WillRepeatedly(
            Return(Result<void>::fail("Already in that folder", MegaErrorCode::kEArgs)));

    EXPECT_TRUE(mutations->canCopyEntriesOn(clipboardEntries({entry("a", 1)}), 7, false));
}

TEST_F(FileMutationControllerTest, CopyEntriesToReadsTheDropTargetNotTheCurrentFolder)
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

    mutations->copyEntriesTo(clipboardEntries({entry("a.txt", 1)}), 7, false);
    flush();
    flush();

    EXPECT_TRUE(sevenRead);
}

TEST_F(FileMutationControllerTest, CopyEntriesToAsksAgainstTheDropTargetsNames)
{
    // A Ctrl+drop raises the same question as a paste, and against the folder
    // the pointer was over rather than the one this tab is showing.
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, getChildren(7u, _, _))
        .WillRepeatedly(InvokeArgument<2>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{entry("a.txt", 90)})));
    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).Times(0);

    mutations->copyEntriesTo(clipboardEntries({entry("a.txt", 1)}), 7, false);
    flush();
    flush();

    ASSERT_EQ(conflictCalls, 1);
    EXPECT_EQ(lastConflictDestination, 7u);
    EXPECT_FALSE(lastConflictDestinationIsRoot);
}

TEST_F(FileMutationControllerTest, CopyRenamingExistingAutoRenamesAgainstTheDropTargetsNames)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, getChildren(7u, _, _))
        .WillRepeatedly(InvokeArgument<2>(
            Result<std::vector<FileEntry>>::ok(std::vector<FileEntry>{entry("a.txt", 90)})));

    mutations->copyEntriesTo(clipboardEntries({entry("a.txt", 1)}), 7, false);
    flush();
    flush();
    ASSERT_EQ(conflictCalls, 1);

    EXPECT_CALL(*client, copyNode(1u, 7u, false, std::string("a - Copy.txt"), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));

    mutations->copyRenamingExisting(lastConflictEntries,
                                    lastConflictDestination,
                                    lastConflictDestinationIsRoot);
    flush();
    flush();

    EXPECT_EQ(operationCalls, 1);
}

TEST_F(FileMutationControllerTest, CopyEntriesToRefusesTheWholeDropWhenTheTargetCantBeRead)
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

    mutations->copyEntriesTo(clipboardEntries({entry("a.txt", 1)}), 7, false);
    flush();
    flush();

    EXPECT_EQ(operationCalls, 0);
    EXPECT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("copy"));
}

TEST_F(FileMutationControllerTest, CopyEntriesToReportsAnErrorWithoutReadingWhenTheTargetRefuses)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, checkUpload(7u, false))
        .WillRepeatedly(Return(Result<void>::fail("read-only", MegaErrorCode::kEAccess)));
    EXPECT_CALL(*client, getChildren(7u, _, _)).Times(0);
    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).Times(0);

    mutations->copyEntriesTo(clipboardEntries({entry("a.txt", 1)}), 7, false);
    flush();

    EXPECT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("copy"));
}

TEST_F(FileMutationControllerTest, CopyEntriesToEmitsNodesCopiedForTheDropTarget)
{
    givenRootListing({});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, getChildren(7u, _, _))
        .WillOnce(InvokeArgument<2>(Result<std::vector<FileEntry>>::ok({})));
    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).WillOnce(InvokeArgument<4>(Result<void>::ok()));

    quint64 destination = 0;
    bool destinationIsRoot = true;
    QObject::connect(mutations.get(),
                     &FileMutationController::nodesCopied,
                     mutations.get(),
                     [&](quint64 handle, bool isRoot) {
                         destination = handle;
                         destinationIsRoot = isRoot;
                     });

    mutations->copyEntriesTo(clipboardEntries({entry("a.txt", 1)}), 7, false);
    flush();
    flush();

    EXPECT_EQ(destination, 7u);
    EXPECT_FALSE(destinationIsRoot);
}

TEST_F(FileMutationControllerTest, CopyEntriesToDoesNotRefetchAListingItDidNotChange)
{
    givenRootListing({entry("a.txt", 1)});
    controller->loadRoot();
    flush();

    EXPECT_CALL(*client, getChildren(7u, _, _))
        .WillOnce(InvokeArgument<2>(Result<std::vector<FileEntry>>::ok({})));
    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).WillOnce(InvokeArgument<4>(Result<void>::ok()));
    const int fetchesBefore = rootFetches;

    mutations->copyEntriesTo(clipboardEntries({entry("a.txt", 1)}), 7, false);
    flush();
    flush();

    // A copy leaves the source folder alone, and this tab is showing the root,
    // not folder 7 -- so nothing here needs re-reading. Other tabs are reached
    // through nodesCopied.
    EXPECT_EQ(rootFetches - fetchesBefore, 0);
}

TEST_F(FileMutationControllerTest, CanPasteIsFalseForACopyOfAFolderIntoItsOwnSubtree)
{
    givenRootListing({});
    controller->loadRoot();
    flush();
    clipboard->copy(clipboardEntries({entry("a", 1, true)}), 7, false);

    EXPECT_CALL(*client, checkMove(1u, _, true))
        .WillRepeatedly(Return(Result<void>::fail("circular", MegaErrorCode::kECircular)));
    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).Times(0);

    EXPECT_FALSE(mutations->canPaste());
    mutations->paste();
    flush();
    flush();

    EXPECT_EQ(errorCalls, 1);
    EXPECT_EQ(lastErrorContext, QStringLiteral("paste"));
}
