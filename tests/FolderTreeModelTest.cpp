#include "qml/FolderTreeModel.h"

#include "core/FolderTreeService.h"
#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"
#include "TestApp.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

// FolderTreeModel is src/qml GUI glue, which this codebase otherwise leaves
// untested by convention -- deliberately bent here, same rationale as
// FileListModel/TabsController: index()/parent() round-tripping and
// load-state bookkeeping are pure and genuinely bug-prone, not rendering
// glue. Builds a real FolderTreeService against MockMegaClient (same
// "real service, mocked SDK" approach as TabsControllerTest) rather than
// mocking FolderTreeService itself.
namespace
{

// ensureLoaded()'s result always arrives via a queued invoke onto the GUI
// thread (see src/qml/GuiThread.h), even though
// MockMegaClient's InvokeArgument action below fires synchronously, so this
// fixture needs the shared QCoreApplication from TestApp.h and an explicit
// flushQueuedEvents() after triggering a load.
class FolderTreeModelTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        testApp();
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
        model = std::make_unique<FolderTreeModel>(service);
    }

    QModelIndex rootIndex() const
    {
        return model->index(0, 0, QModelIndex());
    }

    std::shared_ptr<MockMegaClient> client;
    std::shared_ptr<FolderTreeService> service;
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
        .WillOnce(
            ::testing::InvokeArgument<1>(Result<std::vector<FileEntry>>::fail("network error", MegaErrorCode::kEAgain)));
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
    // reset()/reload() destroy the TreeNode an in-flight load captured, so
    // without FolderTreeModel's generation guard this is a use-after-free,
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
