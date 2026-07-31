#include "core/FileOperationService.h"

#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::_;
using ::testing::InvokeArgument;
using ::testing::Return;

namespace
{

// Captures the single Result the service reports back, so each test can assert
// on it without a member-heavy fixture (same lightweight style as
// FolderNavigationServiceTest.cpp).
struct Capture
{
    int calls = 0;
    Result<void> result;

    std::function<void(Result<void>)> sink()
    {
        return [this](Result<void> r) {
            ++calls;
            result = std::move(r);
        };
    }
};

} // namespace

TEST(FileOperationServiceTest, RejectsAnEmptyNameWithoutCallingTheSdk)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, renameNode(_, _, _)).Times(0);
    Capture captured;

    service.rename(11, "", captured.sink());

    EXPECT_EQ(captured.calls, 1);
    EXPECT_FALSE(captured.result.success);
}

TEST(FileOperationServiceTest, RejectsAWhitespaceOnlyNameWithoutCallingTheSdk)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, renameNode(_, _, _)).Times(0);
    Capture captured;

    service.rename(11, "  \t ", captured.sink());

    EXPECT_EQ(captured.calls, 1);
    EXPECT_FALSE(captured.result.success);
}

TEST(FileOperationServiceTest, RejectsNamesContainingAPathSeparatorWithoutCallingTheSdk)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, renameNode(_, _, _)).Times(0);
    Capture forward;
    Capture back;

    service.rename(11, "a/b", forward.sink());
    service.rename(11, "a\\b", back.sink());

    EXPECT_FALSE(forward.result.success);
    EXPECT_FALSE(back.result.success);
}

TEST(FileOperationServiceTest, PassesAValidNameStraightThrough)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, renameNode(11u, std::string("Report v2.pdf"), _))
        .WillOnce(InvokeArgument<2>(Result<void>::ok()));
    Capture captured;

    service.rename(11, "Report v2.pdf", captured.sink());

    EXPECT_EQ(captured.calls, 1);
    EXPECT_TRUE(captured.result.success);
}

TEST(FileOperationServiceTest, AcceptsNamesWithLeadingOrTrailingSpaces)
{
    // Only all-whitespace is rejected -- trimming is deliberately not this
    // layer's business, so whatever the user typed reaches the SDK verbatim.
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, renameNode(11u, std::string(" spaced "), _))
        .WillOnce(InvokeArgument<2>(Result<void>::ok()));
    Capture captured;

    service.rename(11, " spaced ", captured.sink());

    EXPECT_TRUE(captured.result.success);
}

TEST(FileOperationServiceTest, PropagatesARenameFailure)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, renameNode(11u, _, _))
        .WillOnce(InvokeArgument<2>(Result<void>::fail("access denied", -11)));
    Capture captured;

    service.rename(11, "New name", captured.sink());

    ASSERT_EQ(captured.calls, 1);
    EXPECT_FALSE(captured.result.success);
    EXPECT_EQ(captured.result.errorMessage, "access denied");
    EXPECT_EQ(captured.result.errorCode, -11);
}

TEST(FileOperationServiceTest, MoveToRubbishPassesThrough)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, moveToRubbish(11u, _)).WillOnce(InvokeArgument<1>(Result<void>::ok()));
    Capture captured;

    service.moveToRubbish(11, captured.sink());

    EXPECT_EQ(captured.calls, 1);
    EXPECT_TRUE(captured.result.success);
}

TEST(FileOperationServiceTest, PropagatesAMoveToRubbishFailure)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, moveToRubbish(11u, _))
        .WillOnce(InvokeArgument<1>(Result<void>::fail("no such node")));
    Capture captured;

    service.moveToRubbish(11, captured.sink());

    ASSERT_EQ(captured.calls, 1);
    EXPECT_FALSE(captured.result.success);
    EXPECT_EQ(captured.result.errorMessage, "no such node");
}

TEST(FileOperationServiceTest, MovePassesThroughOnceCanMoveAccepts)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, checkMove(11u, 22u, false)).WillOnce(Return(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(11u, 22u, false, _))
        .WillOnce(InvokeArgument<3>(Result<void>::ok()));
    Capture captured;

    service.move(11, 22, false, captured.sink());

    EXPECT_EQ(captured.calls, 1);
    EXPECT_TRUE(captured.result.success);
}

TEST(FileOperationServiceTest, MoveForwardsTheRootSentinelToTheClient)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, checkMove(11u, _, true)).WillOnce(Return(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(11u, _, true, _))
        .WillOnce(InvokeArgument<3>(Result<void>::ok()));
    Capture captured;

    service.move(11, 0, true, captured.sink());

    EXPECT_TRUE(captured.result.success);
}

TEST(FileOperationServiceTest, RejectsACircularMoveWithoutCallingTheSdk)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, checkMove(11u, 22u, false))
        .WillOnce(Return(Result<void>::fail("circular linkage", MegaErrorCode::kECircular)));
    EXPECT_CALL(*client, moveNode(_, _, _, _)).Times(0);
    Capture captured;

    service.move(11, 22, false, captured.sink());

    ASSERT_EQ(captured.calls, 1);
    EXPECT_FALSE(captured.result.success);
    EXPECT_EQ(captured.result.errorCode, MegaErrorCode::kECircular);
}

TEST(FileOperationServiceTest, RejectsAMoveIntoTheFolderTheNodeIsAlreadyInWithoutCallingTheSdk)
{
    // Dropping onto the folder you dragged out of: kEArgs comes from checkMove,
    // which is the only layer that can see a node's real parent handle.
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, checkMove(11u, 22u, false))
        .WillOnce(Return(Result<void>::fail("already in that folder", MegaErrorCode::kEArgs)));
    EXPECT_CALL(*client, moveNode(_, _, _, _)).Times(0);
    Capture captured;

    service.move(11, 22, false, captured.sink());

    ASSERT_EQ(captured.calls, 1);
    EXPECT_EQ(captured.result.errorCode, MegaErrorCode::kEArgs);
}

TEST(FileOperationServiceTest, RejectsAMoveWhoseDestinationIsGoneWithoutCallingTheSdk)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, checkMove(11u, 22u, false))
        .WillOnce(Return(Result<void>::fail("not found", MegaErrorCode::kENoEnt)));
    EXPECT_CALL(*client, moveNode(_, _, _, _)).Times(0);
    Capture captured;

    service.move(11, 22, false, captured.sink());

    ASSERT_EQ(captured.calls, 1);
    EXPECT_EQ(captured.result.errorCode, MegaErrorCode::kENoEnt);
}

TEST(FileOperationServiceTest, PropagatesAMoveFailureFromTheSdk)
{
    // canMove only sees the local node tree, so a move it accepts can still be
    // refused by the API -- the failure has to survive the second hop too.
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, checkMove(_, _, _)).WillOnce(Return(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(11u, 22u, false, _))
        .WillOnce(InvokeArgument<3>(Result<void>::fail("access denied", MegaErrorCode::kEAccess)));
    Capture captured;

    service.move(11, 22, false, captured.sink());

    ASSERT_EQ(captured.calls, 1);
    EXPECT_FALSE(captured.result.success);
    EXPECT_EQ(captured.result.errorCode, MegaErrorCode::kEAccess);
}

TEST(FileOperationServiceTest, CanMoveIsAPlainPassThroughToTheClient)
{
    // Exposed separately from move() because drop-target feedback queries it on
    // its own, without ever intending to perform the move.
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, checkMove(11u, 22u, false))
        .WillOnce(Return(Result<void>::fail("circular linkage", MegaErrorCode::kECircular)));

    const Result<void> result = service.canMove(11, 22, false);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, MegaErrorCode::kECircular);
}

TEST(FileOperationServiceTest, IsValidNameIsTheSingleRuleDefinition)
{
    // Called directly, since the rule is public for QML-side pre-validation.
    EXPECT_FALSE(FileOperationService::isValidName(""));
    EXPECT_FALSE(FileOperationService::isValidName("   "));
    EXPECT_FALSE(FileOperationService::isValidName("a/b"));
    EXPECT_FALSE(FileOperationService::isValidName("a\\b"));
    EXPECT_TRUE(FileOperationService::isValidName("a"));
    // MEGA allows duplicate names within a folder, so there's nothing else to
    // reject -- characters Windows would refuse locally are still legal here.
    EXPECT_TRUE(FileOperationService::isValidName("a:b?c*"));
}
