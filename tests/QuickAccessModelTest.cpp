#include "qml/QuickAccessModel.h"

#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"
#include "MockPinnedFolderStore.h"
#include "qml/NotificationController.h"
#include "TestApp.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::_;
using ::testing::InvokeArgument;
using ::testing::Return;
using ::testing::SaveArg;

// QuickAccessModel is src/qml GUI glue, which this codebase otherwise leaves
// untested by convention -- deliberately bent here, same rationale as
// FileListModel/TabsController/FolderTreeModel: the login-time validation
// sweep's reconciliation and its generation guard are pure, genuinely
// bug-prone bookkeeping, not rendering glue. Builds a real QuickAccessService
// against MockMegaClient/MockPinnedFolderStore (the same "real service, mocked
// dependencies" approach as FolderTreeModelTest).
namespace
{

PinnedFolder makePin(const char* name, std::uint64_t handle)
{
    PinnedFolder pin;
    pin.name = name;
    pin.handle = handle;
    return pin;
}

Result<NodeInfo> liveFolder(const char* name, std::uint64_t handle)
{
    NodeInfo info;
    info.name = name;
    info.handle = handle;
    info.isFolder = true;
    info.inCloud = true;
    return Result<NodeInfo>::ok(info);
}

Result<NodeInfo> inRubbish(const char* name, std::uint64_t handle)
{
    NodeInfo info;
    info.name = name;
    info.handle = handle;
    info.isFolder = true;
    info.inCloud = false;
    return Result<NodeInfo>::ok(info);
}

// A resolve that couldn't be answered at all -- MegaSdkClient's shut-down
// sentinel, the realistic way this happens. Classified Unknown, never Gone.
Result<NodeInfo> unverifiable()
{
    return Result<NodeInfo>::fail("client is shutting down", 2);
}

class QuickAccessModelTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        testApp();
        client = std::make_shared<MockMegaClient>();
        store = std::make_shared<MockPinnedFolderStore>();
        // Fixed account for every test here (Phase 11a) -- none of this
        // model's own coverage is about account scoping, that's
        // QuickAccessServiceTest's job.
        ON_CALL(*client, currentUserHandle()).WillByDefault(Return(Result<std::uint64_t>::ok(111)));
        service = std::make_shared<QuickAccessService>(client, store);
        model = std::make_unique<QuickAccessModel>(service, &notifications);
    }

    void givenStoredPins(std::vector<PinnedFolder> pins)
    {
        EXPECT_CALL(*store, load(_))
            .WillRepeatedly(Return(Result<std::vector<PinnedFolder>>::ok(std::move(pins))));
    }

    QString nameAt(int row) const
    {
        return model->data(model->index(row, 0), QuickAccessModel::NameRole).toString();
    }

    quint64 handleAt(int row) const
    {
        return model->data(model->index(row, 0), QuickAccessModel::HandleRole).toULongLong();
    }

    NotificationController notifications;
    std::shared_ptr<MockMegaClient> client;
    std::shared_ptr<MockPinnedFolderStore> store;
    std::shared_ptr<QuickAccessService> service;
    std::unique_ptr<QuickAccessModel> model;
};

} // namespace

TEST_F(QuickAccessModelTest, ReloadShowsStoredPinsBeforeValidationCompletes)
{
    givenStoredPins({makePin("Photos", 11), makePin("Work", 22)});
    // Callbacks are captured, never fired, so nothing has been validated yet.
    EXPECT_CALL(*client, getNodeInfo(_, _)).Times(2);

    model->reload();

    // The panel must not be blank while the sweep runs.
    EXPECT_EQ(model->count(), 2);
    EXPECT_EQ(nameAt(0), QStringLiteral("Photos"));
    EXPECT_EQ(handleAt(1), 22u);
}

TEST_F(QuickAccessModelTest, ReloadFollowsARename)
{
    givenStoredPins({makePin("Photos", 11)});
    EXPECT_CALL(*client, getNodeInfo(11u, _))
        .WillOnce(InvokeArgument<1>(liveFolder("Holiday photos", 11)));
    // The refreshed name is written through, so it survives the next restart.
    EXPECT_CALL(*store, save(_, std::vector<PinnedFolder>{makePin("Holiday photos", 11)}))
        .WillOnce(Return(Result<void>::ok()));

    model->reload();
    flushQueuedEvents();

    ASSERT_EQ(model->count(), 1);
    EXPECT_EQ(nameAt(0), QStringLiteral("Holiday photos"));
    // A rename never changes the handle, which is why the pin survived at all.
    EXPECT_EQ(handleAt(0), 11u);
}

TEST_F(QuickAccessModelTest, ReloadDropsAPinWhoseTargetIsInTheRubbishBin)
{
    givenStoredPins({makePin("Photos", 11), makePin("Work", 22)});
    EXPECT_CALL(*client, getNodeInfo(11u, _)).WillOnce(InvokeArgument<1>(inRubbish("Photos", 11)));
    EXPECT_CALL(*client, getNodeInfo(22u, _)).WillOnce(InvokeArgument<1>(liveFolder("Work", 22)));
    EXPECT_CALL(*store, save(_, std::vector<PinnedFolder>{makePin("Work", 22)}))
        .WillOnce(Return(Result<void>::ok()));

    model->reload();
    flushQueuedEvents();

    ASSERT_EQ(model->count(), 1);
    EXPECT_EQ(handleAt(0), 22u);
}

TEST_F(QuickAccessModelTest, ReloadDropsAPinWhoseHandleNoLongerResolves)
{
    givenStoredPins({makePin("Photos", 11)});
    EXPECT_CALL(*client, getNodeInfo(11u, _))
        .WillOnce(
            InvokeArgument<1>(Result<NodeInfo>::fail("no such node", MegaErrorCode::kENoEnt)));
    EXPECT_CALL(*store, save(_, std::vector<PinnedFolder>{})).WillOnce(Return(Result<void>::ok()));

    model->reload();
    flushQueuedEvents();

    EXPECT_EQ(model->count(), 0);
}

// R3-2's whole point. Before the Usable/Gone/Unknown split, an unanswerable
// resolve read as "dangling" and the sweep wrote the emptied list through to
// disk -- so shutting the app down during a login wiped every pin permanently.
TEST_F(QuickAccessModelTest, ReloadKeepsAPinWhoseCheckCouldNotBeAnswered)
{
    givenStoredPins({makePin("Photos", 11)});
    EXPECT_CALL(*client, getNodeInfo(11u, _)).WillOnce(InvokeArgument<1>(unverifiable()));
    EXPECT_CALL(*store, save(_, _)).Times(0);

    model->reload();
    flushQueuedEvents();

    ASSERT_EQ(model->count(), 1);
    EXPECT_EQ(handleAt(0), 11u);
}

TEST_F(QuickAccessModelTest, ReloadDropsOnlyTheGoneOnesWhenSomeChecksFail)
{
    givenStoredPins({makePin("Photos", 11), makePin("Work", 22), makePin("Archive", 33)});
    EXPECT_CALL(*client, getNodeInfo(11u, _)).WillOnce(InvokeArgument<1>(liveFolder("Photos", 11)));
    EXPECT_CALL(*client, getNodeInfo(22u, _)).WillOnce(InvokeArgument<1>(inRubbish("Work", 22)));
    EXPECT_CALL(*client, getNodeInfo(33u, _)).WillOnce(InvokeArgument<1>(unverifiable()));
    // The unverified pin keeps its stored name too: the sweep learned nothing
    // about it, so it contributes nothing.
    EXPECT_CALL(*store,
                save(_, std::vector<PinnedFolder>{makePin("Photos", 11), makePin("Archive", 33)}))
        .WillOnce(Return(Result<void>::ok()));

    model->reload();
    flushQueuedEvents();

    ASSERT_EQ(model->count(), 2);
    EXPECT_EQ(handleAt(0), 11u);
    EXPECT_EQ(handleAt(1), 33u);
}

TEST_F(QuickAccessModelTest, ReloadWithNothingChangedDoesNotRewriteTheStore)
{
    givenStoredPins({makePin("Photos", 11)});
    EXPECT_CALL(*client, getNodeInfo(11u, _)).WillOnce(InvokeArgument<1>(liveFolder("Photos", 11)));
    // The common case: the sweep confirms everything, so it must not churn the
    // store (nor reset the model) for an identical list.
    EXPECT_CALL(*store, save(_, _)).Times(0);

    model->reload();
    flushQueuedEvents();

    EXPECT_EQ(model->count(), 1);
}

TEST_F(QuickAccessModelTest, MoveReordersRowsAndPersists)
{
    givenStoredPins({makePin("Photos", 11), makePin("Work", 22), makePin("Docs", 33)});
    // Captured and never fired: this test is about the drag, not the sweep.
    EXPECT_CALL(*client, getNodeInfo(_, _)).Times(3);
    model->reload();

    int movedCount = 0;
    QObject::connect(model.get(), &QAbstractItemModel::rowsMoved, model.get(), [&]() {
        ++movedCount;
    });
    EXPECT_CALL(*store,
                save(_,
                     std::vector<PinnedFolder>{
                         makePin("Work", 22), makePin("Photos", 11), makePin("Docs", 33)}))
        .WillOnce(Return(Result<void>::ok()));

    model->move(11, 1);

    // A move is announced as one, not as a remove plus an insert: the view
    // keeps its delegates, which is what stops the list flashing mid-drag.
    EXPECT_EQ(movedCount, 1);
    EXPECT_EQ(handleAt(0), 22u);
    EXPECT_EQ(handleAt(1), 11u);
    EXPECT_EQ(nameAt(1), QStringLiteral("Photos"));
}

// R3-3: the failure used to stop at the service, which discarded it. The pin
// stays in the list -- only the write failed -- so the toast is the only thing
// telling the user it won't be there next launch.
TEST_F(QuickAccessModelTest, AFailedWriteRaisesAnErrorToast)
{
    givenStoredPins({});
    model->reload();

    QString context;
    QObject::connect(&notifications,
                     &NotificationController::errorOccurred,
                     model.get(),
                     [&context](const QString& reported) {
                         context = reported;
                     });
    EXPECT_CALL(*store, save(_, _))
        .WillOnce(Return(
            Result<void>::fail("failed to save quick-access pins", MegaErrorCode::kEInternal)));

    model->pin(11, QStringLiteral("Photos"));

    EXPECT_EQ(context, QStringLiteral("quickAccessSave"));
    EXPECT_EQ(model->count(), 1);
}

TEST_F(QuickAccessModelTest, MoveIgnoresAnUnknownHandleOrANoOpDestination)
{
    givenStoredPins({makePin("Photos", 11), makePin("Work", 22)});
    EXPECT_CALL(*client, getNodeInfo(_, _)).Times(2);
    model->reload();

    EXPECT_CALL(*store, save(_, _)).Times(0);

    model->move(99, 0); // never pinned
    model->move(11, 0); // already there

    EXPECT_EQ(handleAt(0), 11u);
    EXPECT_EQ(handleAt(1), 22u);
}

TEST_F(QuickAccessModelTest, MoveClampsADestinationPastTheEndOntoTheLastRow)
{
    givenStoredPins({makePin("Photos", 11), makePin("Work", 22)});
    EXPECT_CALL(*client, getNodeInfo(_, _)).Times(2);
    model->reload();

    EXPECT_CALL(*store, save(_, _)).WillOnce(Return(Result<void>::ok()));

    // What a drag released below the last row asks for.
    model->move(11, 99);

    EXPECT_EQ(handleAt(0), 22u);
    EXPECT_EQ(handleAt(1), 11u);
}

TEST_F(QuickAccessModelTest, AValidationSweepPreservesAReorderMadeWhileItWasInFlight)
{
    givenStoredPins({makePin("Photos", 11), makePin("Work", 22)});
    std::function<void(Result<NodeInfo>)> photosDone;
    std::function<void(Result<NodeInfo>)> workDone;
    EXPECT_CALL(*client, getNodeInfo(11u, _)).WillOnce(SaveArg<1>(&photosDone));
    EXPECT_CALL(*client, getNodeInfo(22u, _)).WillOnce(SaveArg<1>(&workDone));

    model->reload();

    // The drag lands before either resolveFolder has answered.
    EXPECT_CALL(*store,
                save(_, std::vector<PinnedFolder>{makePin("Work", 22), makePin("Photos", 11)}))
        .WillOnce(Return(Result<void>::ok()));
    model->move(11, 1);

    // The sweep confirms both pins, so it has nothing to contribute -- and must
    // not write its own snapshot's order back over the reorder.
    EXPECT_CALL(*store, save(_, _)).Times(0);
    photosDone(liveFolder("Photos", 11));
    workDone(liveFolder("Work", 22));
    flushQueuedEvents();

    ASSERT_EQ(model->count(), 2);
    EXPECT_EQ(handleAt(0), 22u);
    EXPECT_EQ(handleAt(1), 11u);
}

TEST_F(QuickAccessModelTest, ActivateEmitsActivatedForALivePin)
{
    givenStoredPins({makePin("Photos", 11)});
    EXPECT_CALL(*client, getNodeInfo(11u, _))
        .WillRepeatedly(InvokeArgument<1>(liveFolder("Photos", 11)));
    model->reload();
    flushQueuedEvents();

    int activatedCount = 0;
    quint64 activatedHandle = 0;
    bool activatedInNewTab = false;
    int missingCount = 0;
    QObject::connect(
        model.get(), &QuickAccessModel::activated, model.get(), [&](quint64 handle, bool inNewTab) {
            ++activatedCount;
            activatedHandle = handle;
            activatedInNewTab = inNewTab;
        });
    QObject::connect(model.get(), &QuickAccessModel::missing, model.get(), [&](quint64, QString) {
        ++missingCount;
    });

    model->activate(11, true);
    flushQueuedEvents();

    EXPECT_EQ(activatedCount, 1);
    EXPECT_EQ(activatedHandle, 11u);
    EXPECT_TRUE(activatedInNewTab);
    EXPECT_EQ(missingCount, 0);
}

TEST_F(QuickAccessModelTest, ActivateEmitsMissingWithTheClickedLabelForADeletedPin)
{
    givenStoredPins({makePin("Photos", 11)});
    // Alive at login, deleted afterwards -- the case the login-time sweep
    // cannot catch, which is why activate() re-checks at all.
    EXPECT_CALL(*client, getNodeInfo(11u, _))
        .WillOnce(InvokeArgument<1>(liveFolder("Photos", 11)))
        .WillOnce(
            InvokeArgument<1>(Result<NodeInfo>::fail("no such node", MegaErrorCode::kENoEnt)));
    model->reload();
    flushQueuedEvents();

    int missingCount = 0;
    quint64 missingHandle = 0;
    QString missingName;
    QObject::connect(
        model.get(), &QuickAccessModel::missing, model.get(), [&](quint64 handle, QString name) {
            ++missingCount;
            missingHandle = handle;
            missingName = name;
        });

    model->activate(11, false);
    flushQueuedEvents();

    EXPECT_EQ(missingCount, 1);
    EXPECT_EQ(missingHandle, 11u);
    // The label the user actually clicked, so the confirmation dialog can name it.
    EXPECT_EQ(missingName, QStringLiteral("Photos"));
    // Declining is the default: activate() itself never removes the pin.
    EXPECT_EQ(model->count(), 1);
}

// missing() opens a dialog offering to unpin, so a check that simply couldn't
// be answered must not reach it -- nothing here says the folder is gone.
TEST_F(QuickAccessModelTest, ActivateRaisesAToastInsteadOfOfferingToUnpinWhenTheCheckFails)
{
    givenStoredPins({makePin("Photos", 11)});
    EXPECT_CALL(*client, getNodeInfo(11u, _))
        .WillOnce(InvokeArgument<1>(liveFolder("Photos", 11)))
        .WillOnce(InvokeArgument<1>(unverifiable()));
    model->reload();
    flushQueuedEvents();

    int missingCount = 0;
    QObject::connect(model.get(), &QuickAccessModel::missing, model.get(), [&](quint64, QString) {
        ++missingCount;
    });
    QString context;
    QObject::connect(&notifications,
                     &NotificationController::errorOccurred,
                     model.get(),
                     [&context](const QString& reported) {
                         context = reported;
                     });

    model->activate(11, false);
    flushQueuedEvents();

    EXPECT_EQ(missingCount, 0);
    EXPECT_EQ(context, QStringLiteral("quickAccessUnavailable"));
    EXPECT_EQ(model->count(), 1);
}

TEST_F(QuickAccessModelTest, PinAppendsARowAndUnpinRemovesIt)
{
    givenStoredPins({});
    EXPECT_CALL(*store, save(_, _)).WillRepeatedly(Return(Result<void>::ok()));
    model->reload();

    model->pin(11, QStringLiteral("Photos"));
    ASSERT_EQ(model->count(), 1);
    EXPECT_EQ(nameAt(0), QStringLiteral("Photos"));
    EXPECT_TRUE(model->isPinned(11));

    model->unpin(11);
    EXPECT_EQ(model->count(), 0);
    EXPECT_FALSE(model->isPinned(11));
}

TEST_F(QuickAccessModelTest, PinIgnoresADuplicateHandle)
{
    givenStoredPins({});
    EXPECT_CALL(*store, save(_, _)).WillRepeatedly(Return(Result<void>::ok()));
    model->reload();

    model->pin(11, QStringLiteral("Photos"));
    // Would corrupt the view's row bookkeeping if beginInsertRows ran anyway.
    model->pin(11, QStringLiteral("Photos again"));

    EXPECT_EQ(model->count(), 1);
    EXPECT_EQ(nameAt(0), QStringLiteral("Photos"));
}

TEST_F(QuickAccessModelTest, ResetPartWayThroughValidationDiscardsTheStaleResult)
{
    givenStoredPins({makePin("Photos", 11)});
    std::function<void(Result<NodeInfo>)> pendingCallback;
    EXPECT_CALL(*client, getNodeInfo(11u, _)).WillOnce(SaveArg<1>(&pendingCallback));
    // Without the generation guard the stale sweep would reconcile against the
    // now-empty list and write it back.
    EXPECT_CALL(*store, save(_, _)).Times(0);

    model->reload();
    ASSERT_TRUE(static_cast<bool>(pendingCallback));

    model->reset();
    ASSERT_EQ(model->count(), 0);

    pendingCallback(inRubbish("Photos", 11));
    flushQueuedEvents();

    EXPECT_EQ(model->count(), 0);
}
