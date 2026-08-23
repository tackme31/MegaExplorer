#include "qml/PropertiesController.h"

#include "core/MegaErrorCodes.h"
#include "core/NodeDetailsService.h"
#include "MockMegaClient.h"

#include <QCoreApplication>

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace
{

// invokeOnGuiThread posts queued even when the callback already runs on the GUI
// thread, so nothing this controller publishes is visible until the loop turns.
void drainEvents()
{
    QCoreApplication::processEvents();
}

std::vector<PathSegment> pathOf(const std::vector<std::string>& names)
{
    std::vector<PathSegment> segments;
    segments.push_back(PathSegment{"", 0, true, ViewKind::CloudDrive});
    for (const std::string& name : names)
        segments.push_back(PathSegment{name, 7, false, ViewKind::CloudDrive});
    return segments;
}

struct Fixture
{
    std::shared_ptr<MockMegaClient> client = std::make_shared<MockMegaClient>();
    std::shared_ptr<NodeDetailsService> service = std::make_shared<NodeDetailsService>(client);
    PropertiesController controller{service};
};

} // namespace

TEST(PropertiesControllerTest, PublishesTheRowsOwnFactsBeforeTheLookupAnswers)
{
    Fixture f;
    // Nothing is delivered: the lookup stays in flight, which is the state under
    // test.
    EXPECT_CALL(*f.client, getPath(::testing::_, ::testing::_, ::testing::_));

    int shown = 0;
    QObject::connect(&f.controller, &PropertiesController::showRequested, [&shown] {
        ++shown;
    });
    f.controller.show(42, false, QStringLiteral("a.jpg"), false, 1024, 1700000000);

    EXPECT_EQ(shown, 1);
    EXPECT_EQ(f.controller.name(), QStringLiteral("a.jpg"));
    EXPECT_FALSE(f.controller.isFolder());
    EXPECT_EQ(f.controller.sizeBytes(), 1024u);
    EXPECT_EQ(f.controller.modificationTime(), 1700000000);
    EXPECT_TRUE(f.controller.loading());
    EXPECT_FALSE(f.controller.failed());
    EXPECT_EQ(f.controller.fileCount(), -1);
}

TEST(PropertiesControllerTest, IgnoresTheRowsSizeForAFolderAndTakesTheRecursiveTotal)
{
    Fixture f;
    EXPECT_CALL(*f.client, getPath(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(
            ::testing::InvokeArgument<2>(Result<std::vector<PathSegment>>::ok(pathOf({"photos"}))));
    EXPECT_CALL(*f.client, getFolderInfo(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<FolderInfo>::ok(FolderInfo{5, 2, 4096})));

    // 999 is what a folder row claims; MEGA gives folders no size of their own.
    f.controller.show(42, false, QStringLiteral("photos"), true, 999, 0);
    EXPECT_EQ(f.controller.sizeBytes(), 0u);

    drainEvents();

    EXPECT_FALSE(f.controller.loading());
    EXPECT_EQ(f.controller.sizeBytes(), 4096u);
    EXPECT_EQ(f.controller.fileCount(), 5);
    EXPECT_EQ(f.controller.folderCount(), 2);
    EXPECT_EQ(f.controller.parentPath(), QString());
}

TEST(PropertiesControllerTest, ReportsAFailedLookupWithoutClearingWhatTheRowGave)
{
    Fixture f;
    EXPECT_CALL(*f.client, getPath(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<std::vector<PathSegment>>::fail("gone", MegaErrorCode::kENoEnt)));

    f.controller.show(42, false, QStringLiteral("a.jpg"), false, 1024, 0);
    drainEvents();

    EXPECT_TRUE(f.controller.failed());
    EXPECT_FALSE(f.controller.loading());
    EXPECT_EQ(f.controller.name(), QStringLiteral("a.jpg"));
    EXPECT_EQ(f.controller.sizeBytes(), 1024u);
}

TEST(PropertiesControllerTest, DropsAReplyBelongingToAPreviouslyInspectedNode)
{
    Fixture f;
    // The first lookup's callback is captured and fired only after the second
    // show() -- the ordering that would otherwise repaint the dialog with the
    // node the user has already navigated away from.
    std::function<void(Result<std::vector<PathSegment>>)> firstReply;
    EXPECT_CALL(*f.client, getPath(1u, ::testing::_, ::testing::_))
        .WillOnce(::testing::SaveArg<2>(&firstReply));
    EXPECT_CALL(*f.client, getPath(2u, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<std::vector<PathSegment>>::ok(pathOf({"second", "b.jpg"}))));

    f.controller.show(1, false, QStringLiteral("a.jpg"), false, 1, 0);
    f.controller.show(2, false, QStringLiteral("b.jpg"), false, 2, 0);
    drainEvents();
    ASSERT_EQ(f.controller.parentPath(), QStringLiteral("/second"));

    ASSERT_TRUE(firstReply);
    firstReply(Result<std::vector<PathSegment>>::ok(pathOf({"first", "a.jpg"})));
    drainEvents();

    EXPECT_EQ(f.controller.name(), QStringLiteral("b.jpg"));
    EXPECT_EQ(f.controller.parentPath(), QStringLiteral("/second"));
}

TEST(PropertiesControllerTest, AsksTheDialogToOpenAgainForTheSameNode)
{
    Fixture f;
    EXPECT_CALL(*f.client, getPath(::testing::_, ::testing::_, ::testing::_))
        .WillRepeatedly(
            ::testing::InvokeArgument<2>(Result<std::vector<PathSegment>>::ok(pathOf({"a.jpg"}))));

    int shown = 0;
    QObject::connect(&f.controller, &PropertiesController::showRequested, [&shown] {
        ++shown;
    });
    f.controller.show(42, false, QStringLiteral("a.jpg"), false, 1, 0);
    f.controller.show(42, false, QStringLiteral("a.jpg"), false, 1, 0);
    drainEvents();

    EXPECT_EQ(shown, 2);
}
