#include "qml/FolderTreeModel.h"

#include "core/FolderTreeService.h"
#include "core/MegaErrorCodes.h"
#include "qml/NotificationController.h"
#include "MockMegaClient.h"
#include "TestApp.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace
{

// ensureLoaded()'s result always arrives via a queued invoke onto the GUI
// thread (see src/qml/GuiThread.h), even though
// MockMegaClient's InvokeArgument action below fires synchronously, so this
// fixture needs an explicit flushQueuedEvents() after triggering a load --
// twice after refreshFolder(), which hops once for the catchup and again for
// the re-read that catchup starts.
class FolderTreeModelTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        client = std::make_shared<MockMegaClient>();
        // TRAP: Result<bool>::success defaults to false (src/core/Result.h), so
        // gmock's default action for an unstubbed hasSubfolders() is *failure*,
        // which FolderTreeService reads as "no subfolders" -- every unloaded
        // node would then report hasChildren() == false and the failures would
        // look unrelated to this line. Same blanket-expectation shape
        // UploadServiceTest uses for checkUpload(). Tests wanting the opposite
        // declare their own, later, EXPECT_CALL: gmock matches newest-first.
        EXPECT_CALL(*client, hasSubfolders(::testing::_, ::testing::_))
            .Times(::testing::AnyNumber())
            .WillRepeatedly(::testing::Return(Result<bool>::ok(true)));
        service = std::make_shared<FolderTreeService>(client);
        model = std::make_unique<FolderTreeModel>(service, &notifications);
    }

    // Loads the root with one child folder, so a refresh has something to replace.
    void loadRootWith(std::vector<FileEntry> children)
    {
        EXPECT_CALL(*client, getRootChildren(::testing::_, ::testing::_))
            .WillOnce(::testing::InvokeArgument<1>(
                Result<std::vector<FileEntry>>::ok(std::move(children))));
        model->ensureLoaded(rootIndex());
        flushQueuedEvents();
    }

    QModelIndex rootIndex() const
    {
        return model->index(0, 0, QModelIndex());
    }

    std::shared_ptr<MockMegaClient> client;
    std::shared_ptr<FolderTreeService> service;
    NotificationController notifications;
    std::unique_ptr<FolderTreeModel> model;
};

} // namespace

TEST_F(FolderTreeModelTest, InitialStateIsSingleRootRow)
{
    EXPECT_EQ(model->rowCount(QModelIndex()), 1);

    const QModelIndex root = rootIndex();
    ASSERT_TRUE(root.isValid());
    EXPECT_EQ(model->data(root, Qt::DisplayRole).toString(), QStringLiteral("Cloud Drive"));
    EXPECT_TRUE(model->data(root, FolderTreeModel::IsRootRole).toBool());
}

TEST_F(FolderTreeModelTest, UnloadedRootHasChildrenAndCanFetchMore)
{
    const QModelIndex root = rootIndex();
    EXPECT_TRUE(model->hasChildren(root));
    EXPECT_TRUE(model->canFetchMore(root));
}

TEST_F(FolderTreeModelTest, UnloadedNodeWithoutSubfoldersReportsNoChildren)
{
    // The expand arrow used to appear on every not-yet-loaded node, so a folder
    // holding only files still got one. Overrides SetUp()'s blanket answer.
    EXPECT_CALL(*client, hasSubfolders(::testing::_, ::testing::_))
        .WillRepeatedly(::testing::Return(Result<bool>::ok(false)));

    const QModelIndex root = rootIndex();
    EXPECT_FALSE(model->hasChildren(root));
    // Still fetchable: canFetchMore is about load state, not about the answer.
    EXPECT_TRUE(model->canFetchMore(root));
}

TEST_F(FolderTreeModelTest, EnsureLoadedDoesNotReenterWhileLoading)
{
    // No WillOnce/InvokeArgument: the callback deliberately never fires, so
    // the node stays stuck in Loading across both calls below -- proving the
    // second ensureLoaded() is a no-op (Times(1) below would otherwise fail).
    EXPECT_CALL(*client, getRootChildren(::testing::_, ::testing::_)).Times(1);

    const QModelIndex root = rootIndex();
    model->ensureLoaded(root);
    model->ensureLoaded(root);
}

TEST_F(FolderTreeModelTest, EnsureLoadedPopulatesChildrenOnSuccessInNameOrder)
{
    const std::vector<FileEntry> children{
        {"Alpha", 10, 0, true, 0},
        {"Beta", 11, 0, true, 0},
    };
    EXPECT_CALL(*client, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(children)));

    const QModelIndex root = rootIndex();
    model->ensureLoaded(root);
    flushQueuedEvents();

    ASSERT_EQ(model->rowCount(root), 2);
    const QModelIndex child0 = model->index(0, 0, root);
    EXPECT_EQ(model->data(child0, FolderTreeModel::NameRole).toString(), QStringLiteral("Alpha"));
    EXPECT_EQ(model->data(child0, FolderTreeModel::HandleRole).toULongLong(),
              static_cast<qulonglong>(10));
    EXPECT_EQ(model->parent(child0), root);
    EXPECT_FALSE(model->canFetchMore(root));
}

TEST_F(FolderTreeModelTest, EnsureLoadedFailureResetsToNotLoadedForRetry)
{
    const QModelIndex root = rootIndex();

    EXPECT_CALL(*client, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(
            Result<std::vector<FileEntry>>::fail("network error", MegaErrorCode::kEAgain)));
    model->ensureLoaded(root);
    flushQueuedEvents();

    EXPECT_TRUE(model->canFetchMore(root));
    EXPECT_EQ(model->rowCount(root), 0);

    const std::vector<FileEntry> children{{"Alpha", 10, 0, true, 0}};
    EXPECT_CALL(*client, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(children)));
    model->ensureLoaded(root);
    flushQueuedEvents();

    EXPECT_EQ(model->rowCount(root), 1);
}

TEST_F(FolderTreeModelTest, ResetWhileLoadingDropsTheInFlightResult)
{
    // reset()/reload() destroy the TreeNode an in-flight load started on, so
    // without FolderTreeModel's load-token guard this is a use-after-free,
    // not just a stale insert.
    std::function<void(Result<std::vector<FileEntry>>)> pendingCallback;
    EXPECT_CALL(*client, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<1>(&pendingCallback));

    model->ensureLoaded(rootIndex());
    model->reset();

    pendingCallback(Result<std::vector<FileEntry>>::ok({{"Alpha", 10, 0, true, 0}}));
    flushQueuedEvents();

    EXPECT_EQ(model->rowCount(rootIndex()), 0);
    EXPECT_TRUE(model->canFetchMore(rootIndex()));
}

TEST_F(FolderTreeModelTest, ResetCollapsesBackToSingleUnloadedRootRow)
{
    const std::vector<FileEntry> children{{"Alpha", 10, 0, true, 0}};
    EXPECT_CALL(*client, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok(children)));
    model->ensureLoaded(rootIndex());
    flushQueuedEvents();
    ASSERT_EQ(model->rowCount(rootIndex()), 1);

    model->reset();

    EXPECT_EQ(model->rowCount(QModelIndex()), 1);
    EXPECT_EQ(model->rowCount(rootIndex()), 0);
    EXPECT_TRUE(model->canFetchMore(rootIndex()));
}

TEST_F(FolderTreeModelTest, RefreshFolderReplacesTheChildrenAndKeepsTheRow)
{
    loadRootWith({{"Alpha", 10, 0, true, 0}});

    EXPECT_CALL(*client, syncPendingChanges(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(Result<void>::ok()));
    EXPECT_CALL(*client, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(
            Result<std::vector<FileEntry>>::ok({{"Beta", 11, 0, true, 0}})));

    model->refreshFolder(0, true);
    flushQueuedEvents();
    flushQueuedEvents();

    // The row asked about is still there -- only what hangs below it was replaced.
    EXPECT_EQ(model->rowCount(QModelIndex()), 1);
    ASSERT_EQ(model->rowCount(rootIndex()), 1);
    EXPECT_EQ(model->data(model->index(0, 0, rootIndex()), FolderTreeModel::NameRole).toString(),
              QStringLiteral("Beta"));
}

TEST_F(FolderTreeModelTest, RefreshFolderCatchesUpWithTheServerFirst)
{
    // Without the catchup the re-read hands back the same cached children, so the
    // ordering here is the whole point of the action.
    loadRootWith({{"Alpha", 10, 0, true, 0}});

    ::testing::InSequence order;
    EXPECT_CALL(*client, syncPendingChanges(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(Result<void>::ok()));
    EXPECT_CALL(*client, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::ok({})));

    model->refreshFolder(0, true);
    flushQueuedEvents();
    flushQueuedEvents();
}

TEST_F(FolderTreeModelTest, RefreshFolderReportsAFolderThatNoLongerExists)
{
    loadRootWith({{"Alpha", 10, 0, true, 0}});
    const QModelIndex child = model->index(0, 0, rootIndex());
    ASSERT_TRUE(child.isValid());

    // Deliberately not QSignalSpy: that lives in Qt6::Test, which this target
    // doesn't link.
    int errors = 0;
    QString context;
    NotificationController::ErrorReason reason = NotificationController::Unknown;
    QObject::connect(&notifications,
                     &NotificationController::errorOccurred,
                     &notifications,
                     [&](QString c, NotificationController::ErrorReason r, QString) {
                         ++errors;
                         context = std::move(c);
                         reason = r;
                     });

    EXPECT_CALL(*client, syncPendingChanges(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(Result<void>::ok()));
    EXPECT_CALL(*client, getChildren(10, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<std::vector<FileEntry>>::fail("not found", MegaErrorCode::kENoEnt)));

    model->refreshFolder(10, false);
    flushQueuedEvents();
    flushQueuedEvents();

    ASSERT_EQ(errors, 1);
    EXPECT_EQ(context, QStringLiteral("refresh"));
    EXPECT_EQ(reason, NotificationController::NotFound);
    // The row stays: a refresh reports the gap rather than pruning the tree.
    EXPECT_EQ(model->rowCount(rootIndex()), 1);
}

TEST_F(FolderTreeModelTest, ExpandFailureStaysSilent)
{
    // Only a refresh the user asked for toasts; an expand that fails shows itself
    // by not opening.
    int errors = 0;
    QObject::connect(&notifications,
                     &NotificationController::errorOccurred,
                     &notifications,
                     [&errors](QString, NotificationController::ErrorReason, QString) {
                         ++errors;
                     });

    EXPECT_CALL(*client, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(
            Result<std::vector<FileEntry>>::fail("not found", MegaErrorCode::kENoEnt)));

    model->ensureLoaded(rootIndex());
    flushQueuedEvents();

    EXPECT_EQ(errors, 0);
}

TEST_F(FolderTreeModelTest, RefreshFolderDropsAnInFlightExpandOfTheSameNode)
{
    // The refresh restarts the load, so the earlier one must not also insert --
    // that would double every row.
    std::function<void(Result<std::vector<FileEntry>>)> pendingExpand;
    EXPECT_CALL(*client, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<1>(&pendingExpand))
        .WillOnce(::testing::InvokeArgument<1>(
            Result<std::vector<FileEntry>>::ok({{"Beta", 11, 0, true, 0}})));
    EXPECT_CALL(*client, syncPendingChanges(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(Result<void>::ok()));

    model->ensureLoaded(rootIndex());
    model->refreshFolder(0, true);
    flushQueuedEvents();
    flushQueuedEvents();

    pendingExpand(Result<std::vector<FileEntry>>::ok({{"Alpha", 10, 0, true, 0}}));
    flushQueuedEvents();

    ASSERT_EQ(model->rowCount(rootIndex()), 1);
    EXPECT_EQ(model->data(model->index(0, 0, rootIndex()), FolderTreeModel::NameRole).toString(),
              QStringLiteral("Beta"));
}

TEST_F(FolderTreeModelTest, FailedRefreshStillTellsTheViewTheRowChanged)
{
    // The removal leaves the view believing the row has no children; only the
    // reload's own signal takes that back, and a failed reload inserts nothing.
    loadRootWith({{"Alpha", 10, 0, true, 0}});

    int changes = 0;
    QObject::connect(model.get(),
                     &QAbstractItemModel::dataChanged,
                     model.get(),
                     [&changes](const QModelIndex&, const QModelIndex&, const QList<int>&) {
                         ++changes;
                     });

    EXPECT_CALL(*client, syncPendingChanges(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(Result<void>::ok()));
    EXPECT_CALL(*client, getRootChildren(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(
            Result<std::vector<FileEntry>>::fail("network error", MegaErrorCode::kEAgain)));

    model->refreshFolder(0, true);
    flushQueuedEvents();
    flushQueuedEvents();

    EXPECT_EQ(changes, 1);
    // And the row is fetchable again, so the arrow the view redraws still works.
    EXPECT_TRUE(model->canFetchMore(rootIndex()));
}

TEST_F(FolderTreeModelTest, RefreshFolderDropsItsOwnResultWhenTheTreeWasResetMeanwhile)
{
    // reset() rebuilds the root with the same handle 0 / isRoot pair, so looking the
    // node up again is not enough to tell it apart from the one this refresh began
    // on -- only the token can.
    loadRootWith({{"Alpha", 10, 0, true, 0}});

    std::function<void(Result<void>)> pendingSync;
    EXPECT_CALL(*client, syncPendingChanges(::testing::_))
        .WillOnce(::testing::SaveArg<0>(&pendingSync));
    // Never re-read: the refresh must give up before it gets that far.
    EXPECT_CALL(*client, getRootChildren(::testing::_, ::testing::_)).Times(0);

    model->refreshFolder(0, true);
    model->reset();

    pendingSync(Result<void>::ok());
    flushQueuedEvents();
    flushQueuedEvents();

    EXPECT_EQ(model->rowCount(rootIndex()), 0);
    EXPECT_TRUE(model->canFetchMore(rootIndex()));
}
