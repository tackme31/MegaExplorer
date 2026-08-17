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
    EXPECT_CALL(*client, siblingNameTaken(11u, std::string("Report v2.pdf")))
        .WillOnce(Return(Result<bool>::ok(false)));
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
    EXPECT_CALL(*client, siblingNameTaken(_, _)).WillOnce(Return(Result<bool>::ok(false)));
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
    EXPECT_CALL(*client, siblingNameTaken(_, _)).WillOnce(Return(Result<bool>::ok(false)));
    EXPECT_CALL(*client, renameNode(11u, _, _))
        .WillOnce(InvokeArgument<2>(Result<void>::fail("access denied", -11)));
    Capture captured;

    service.rename(11, "New name", captured.sink());

    ASSERT_EQ(captured.calls, 1);
    EXPECT_FALSE(captured.result.success);
    EXPECT_EQ(captured.result.errorMessage, "access denied");
    EXPECT_EQ(captured.result.errorCode, MegaErrorCode::kEAccess);
}

TEST(FileOperationServiceTest, RefusesARenameToANameASiblingAlreadyHas)
{
    // kEExist, the code the server uses for a duplicate folder, so the caller can
    // tell it apart from isValidName()'s kEArgs.
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, siblingNameTaken(11u, std::string("taken.txt")))
        .WillOnce(Return(Result<bool>::ok(true)));
    EXPECT_CALL(*client, renameNode(_, _, _)).Times(0);
    Capture captured;

    service.rename(11, "taken.txt", captured.sink());

    ASSERT_EQ(captured.calls, 1);
    EXPECT_FALSE(captured.result.success);
    EXPECT_EQ(captured.result.errorCode, MegaErrorCode::kEExist);
}

TEST(FileOperationServiceTest, RenamesAnywayWhenTheSiblingCheckItselfFails)
{
    // A lookup that broke says nothing about the name, and refusing on it would
    // make an unreachable node unrenameable for a reason the user can't act on.
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, siblingNameTaken(_, _))
        .WillOnce(Return(Result<bool>::fail("gone", MegaErrorCode::kENoEnt)));
    EXPECT_CALL(*client, renameNode(11u, std::string("b"), _))
        .WillOnce(InvokeArgument<2>(Result<void>::ok()));
    Capture captured;

    service.rename(11, "b", captured.sink());

    EXPECT_TRUE(captured.result.success);
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
        .WillOnce(InvokeArgument<1>(Result<void>::fail("no such node", MegaErrorCode::kENoEnt)));
    Capture captured;

    service.moveToRubbish(11, captured.sink());

    ASSERT_EQ(captured.calls, 1);
    EXPECT_FALSE(captured.result.success);
    EXPECT_EQ(captured.result.errorMessage, "no such node");
}

TEST(FileOperationServiceTest, SetFavouritePassesBothValuesThrough)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, setNodeFavourite(11u, true, _))
        .WillOnce(InvokeArgument<2>(Result<void>::ok()));
    EXPECT_CALL(*client, setNodeFavourite(11u, false, _))
        .WillOnce(InvokeArgument<2>(Result<void>::ok()));
    Capture captured;

    service.setFavourite(11, true, captured.sink());
    service.setFavourite(11, false, captured.sink());

    EXPECT_EQ(captured.calls, 2);
    EXPECT_TRUE(captured.result.success);
}

TEST(FileOperationServiceTest, PropagatesASetFavouriteFailure)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, setNodeFavourite(11u, true, _))
        .WillOnce(InvokeArgument<2>(Result<void>::fail("no such node", MegaErrorCode::kENoEnt)));
    Capture captured;

    service.setFavourite(11, true, captured.sink());

    ASSERT_EQ(captured.calls, 1);
    EXPECT_FALSE(captured.result.success);
    EXPECT_EQ(captured.result.errorCode, MegaErrorCode::kENoEnt);
}

TEST(FileOperationServiceTest, ExportLinkHandsBackTheUrlTheSdkReturned)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, exportNode(11u, _))
        .WillOnce(InvokeArgument<1>(Result<std::string>::ok("https://mega.nz/file/abc#key")));
    int calls = 0;
    Result<std::string> got = Result<std::string>::fail("unset", 0);

    service.exportLink(11, [&](Result<std::string> r) {
        ++calls;
        got = std::move(r);
    });

    ASSERT_EQ(calls, 1);
    ASSERT_TRUE(got.success);
    EXPECT_EQ(got.value(), "https://mega.nz/file/abc#key");
}

TEST(FileOperationServiceTest, RemoveLinkPassesThroughToDisableExport)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, disableExport(11u, _)).WillOnce(InvokeArgument<1>(Result<void>::ok()));
    Capture captured;

    service.removeLink(11, captured.sink());

    ASSERT_EQ(captured.calls, 1);
    EXPECT_TRUE(captured.result.success);
}

TEST(FileOperationServiceTest, MovePassesThroughOnceCanMoveAccepts)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, checkMove(11u, 22u, false)).WillOnce(Return(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(11u, 22u, false, "", _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));
    Capture captured;

    service.move(11, 22, false, "", captured.sink());

    EXPECT_EQ(captured.calls, 1);
    EXPECT_TRUE(captured.result.success);
}

TEST(FileOperationServiceTest, MoveForwardsTheRootSentinelToTheClient)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, checkMove(11u, _, true)).WillOnce(Return(Result<void>::ok()));
    EXPECT_CALL(*client, moveNode(11u, _, true, "", _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));
    Capture captured;

    service.move(11, 0, true, "", captured.sink());

    EXPECT_TRUE(captured.result.success);
}

TEST(FileOperationServiceTest, RejectsACircularMoveWithoutCallingTheSdk)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, checkMove(11u, 22u, false))
        .WillOnce(Return(Result<void>::fail("circular linkage", MegaErrorCode::kECircular)));
    EXPECT_CALL(*client, moveNode(_, _, _, _, _)).Times(0);
    Capture captured;

    service.move(11, 22, false, "", captured.sink());

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
    EXPECT_CALL(*client, moveNode(_, _, _, _, _)).Times(0);
    Capture captured;

    service.move(11, 22, false, "", captured.sink());

    ASSERT_EQ(captured.calls, 1);
    EXPECT_EQ(captured.result.errorCode, MegaErrorCode::kEArgs);
}

TEST(FileOperationServiceTest, RejectsAMoveWhoseDestinationIsGoneWithoutCallingTheSdk)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, checkMove(11u, 22u, false))
        .WillOnce(Return(Result<void>::fail("not found", MegaErrorCode::kENoEnt)));
    EXPECT_CALL(*client, moveNode(_, _, _, _, _)).Times(0);
    Capture captured;

    service.move(11, 22, false, "", captured.sink());

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
    EXPECT_CALL(*client, moveNode(11u, 22u, false, "", _))
        .WillOnce(InvokeArgument<4>(Result<void>::fail("access denied", MegaErrorCode::kEAccess)));
    Capture captured;

    service.move(11, 22, false, "", captured.sink());

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

TEST(FileOperationServiceTest, CreateFolderPassesAValidNameStraightThrough)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, createFolder(22u, false, std::string("Reports"), _))
        .WillOnce(InvokeArgument<3>(Result<void>::ok()));
    Capture captured;

    service.createFolder(22, false, "Reports", captured.sink());

    EXPECT_EQ(captured.calls, 1);
    EXPECT_TRUE(captured.result.success);
}

TEST(FileOperationServiceTest, CreateFolderRejectsAnInvalidNameWithoutCallingTheSdk)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, createFolder(_, _, _, _)).Times(0);
    Capture captured;

    service.createFolder(22, false, "a/b", captured.sink());

    EXPECT_EQ(captured.calls, 1);
    EXPECT_FALSE(captured.result.success);
    // Tagged so a caller can tell "fix the name you typed" apart from a
    // server-side rejection -- FolderNavigationController branches on it.
    EXPECT_EQ(captured.result.errorCode, MegaErrorCode::kEArgs);
}

TEST(FileOperationServiceTest, CreateFolderPropagatesTheServersDuplicateNameRejection)
{
    // The only duplicate-name check there is: no in-memory pre-check exists,
    // by design (see IMegaClient::createFolder).
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, createFolder(22u, false, _, _))
        .WillOnce(InvokeArgument<3>(Result<void>::fail("already exists", MegaErrorCode::kEExist)));
    Capture captured;

    service.createFolder(22, false, "Reports", captured.sink());

    EXPECT_EQ(captured.calls, 1);
    EXPECT_FALSE(captured.result.success);
    EXPECT_EQ(captured.result.errorCode, MegaErrorCode::kEExist);
}

TEST(FileOperationServiceTest, CreateFolderForwardsTheRootSentinel)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, createFolder(0u, true, std::string("Reports"), _))
        .WillOnce(InvokeArgument<3>(Result<void>::ok()));
    Capture captured;

    service.createFolder(0, true, "Reports", captured.sink());

    EXPECT_TRUE(captured.result.success);
}

TEST(FileOperationServiceTest, CanAddChildrenIsAPlainPassThroughToTheClient)
{
    // Paste's counterpart to canMove above, and queried the same way: once, up
    // front, without intending to perform anything yet.
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, checkUpload(22u, false))
        .WillOnce(Return(Result<void>::fail("read-only share", MegaErrorCode::kEAccess)));

    const Result<void> result = service.canAddChildren(22, false);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.errorCode, MegaErrorCode::kEAccess);
}

TEST(FileOperationServiceTest, CopyPassesAnEmptyNameThroughUnchanged)
{
    // Empty means "keep the source's name", which has to survive the name
    // validation that would otherwise reject it (see FileOperationService::copy).
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, checkMove(_, _, _)).WillRepeatedly(Return(Result<void>::ok()));
    EXPECT_CALL(*client, copyNode(11u, 22u, false, std::string(), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));
    Capture captured;

    service.copy(11, 22, false, "", captured.sink());

    EXPECT_TRUE(captured.result.success);
}

TEST(FileOperationServiceTest, CopyPassesAChosenNameStraightThrough)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, checkMove(_, _, _)).WillRepeatedly(Return(Result<void>::ok()));
    EXPECT_CALL(*client, copyNode(11u, 0u, true, std::string("a - Copy.txt"), _))
        .WillOnce(InvokeArgument<4>(Result<void>::ok()));
    Capture captured;

    service.copy(11, 0, true, "a - Copy.txt", captured.sink());

    EXPECT_TRUE(captured.result.success);
}

TEST(FileOperationServiceTest, CopyRejectsAPathSeparatorInTheNewNameWithoutCallingTheSdk)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).Times(0);
    Capture captured;

    service.copy(11, 22, false, "a/b", captured.sink());

    ASSERT_EQ(captured.calls, 1);
    EXPECT_FALSE(captured.result.success);
    EXPECT_EQ(captured.result.errorCode, MegaErrorCode::kEArgs);
}

TEST(FileOperationServiceTest, PropagatesACopyFailureFromTheSdk)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, checkMove(_, _, _)).WillRepeatedly(Return(Result<void>::ok()));
    EXPECT_CALL(*client, copyNode(_, _, _, _, _))
        .WillOnce(InvokeArgument<4>(Result<void>::fail("node deleted", MegaErrorCode::kENoEnt)));
    Capture captured;

    service.copy(11, 22, false, "", captured.sink());

    ASSERT_EQ(captured.calls, 1);
    EXPECT_EQ(captured.result.errorCode, MegaErrorCode::kENoEnt);
}

TEST(FileOperationServiceTest, CanCopyTreatsAlreadyInThatFolderAsAllowed)
{
    // The one code canCopy reinterprets: a move there is a no-op, a copy there
    // is the duplicate-in-place case and lands a "... - Copy" sibling.
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, checkMove(11u, 22u, false))
        .WillOnce(Return(Result<void>::fail("Already in that folder", MegaErrorCode::kEArgs)));

    EXPECT_TRUE(service.canCopy(11, 22, false).success);
}

TEST(FileOperationServiceTest, CanCopyRefusesAFolderIntoItsOwnSubtree)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, checkMove(11u, 22u, false))
        .WillOnce(Return(Result<void>::fail("circular", MegaErrorCode::kECircular)));

    const Result<void> allowed = service.canCopy(11, 22, false);
    EXPECT_FALSE(allowed.success);
    EXPECT_EQ(allowed.errorCode, MegaErrorCode::kECircular);
}

TEST(FileOperationServiceTest, CanCopyPassesTheOtherRefusalsThrough)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, checkMove(11u, 22u, false))
        .WillOnce(Return(Result<void>::fail("gone", MegaErrorCode::kENoEnt)))
        .WillOnce(Return(Result<void>::fail("read-only", MegaErrorCode::kEAccess)));

    EXPECT_EQ(service.canCopy(11, 22, false).errorCode, MegaErrorCode::kENoEnt);
    EXPECT_EQ(service.canCopy(11, 22, false).errorCode, MegaErrorCode::kEAccess);
}

TEST(FileOperationServiceTest, CopyRefusesWithoutCallingTheSdkWhenCanCopySaysNo)
{
    auto client = std::make_shared<MockMegaClient>();
    FileOperationService service(client);
    EXPECT_CALL(*client, checkMove(_, _, _))
        .WillOnce(Return(Result<void>::fail("circular", MegaErrorCode::kECircular)));
    EXPECT_CALL(*client, copyNode(_, _, _, _, _)).Times(0);
    Capture captured;

    service.copy(11, 22, false, "", captured.sink());

    ASSERT_EQ(captured.calls, 1);
    EXPECT_EQ(captured.result.errorCode, MegaErrorCode::kECircular);
}

TEST(FileOperationServiceTest, UniqueCopyNameLeavesAFreeNameAlone)
{
    // The common case: a copy into a different folder keeps its name.
    EXPECT_EQ(FileOperationService::uniqueCopyName("report.pdf", false, {"other.pdf"}),
              "report.pdf");
}

TEST(FileOperationServiceTest, UniqueCopyNameInsertsCopyBeforeTheExtension)
{
    EXPECT_EQ(FileOperationService::uniqueCopyName("report.pdf", false, {"report.pdf"}),
              "report - Copy.pdf");
}

TEST(FileOperationServiceTest, UniqueCopyNameNumbersEveryFurtherCopy)
{
    EXPECT_EQ(FileOperationService::uniqueCopyName(
                  "report.pdf", false, {"report.pdf", "report - Copy.pdf"}),
              "report - Copy (2).pdf");
    EXPECT_EQ(
        FileOperationService::uniqueCopyName(
            "report.pdf", false, {"report.pdf", "report - Copy.pdf", "report - Copy (2).pdf"}),
        "report - Copy (3).pdf");
}

TEST(FileOperationServiceTest, UniqueCopyNameSplitsAtTheLastDotOnly)
{
    EXPECT_EQ(FileOperationService::uniqueCopyName("archive.tar.gz", false, {"archive.tar.gz"}),
              "archive.tar - Copy.gz");
}

TEST(FileOperationServiceTest, UniqueCopyNameTreatsALeadingDotAsPartOfTheName)
{
    // ".gitignore" is a name, not an extension on an empty stem.
    EXPECT_EQ(FileOperationService::uniqueCopyName(".gitignore", false, {".gitignore"}),
              ".gitignore - Copy");
}

TEST(FileOperationServiceTest, UniqueCopyNameHandlesANameWithNoExtension)
{
    EXPECT_EQ(FileOperationService::uniqueCopyName("README", false, {"README"}), "README - Copy");
}

TEST(FileOperationServiceTest, UniqueCopyNameNeverSplitsAFolderNameAtADot)
{
    // Folders have no extension, so a dotted folder name must stay intact.
    EXPECT_EQ(FileOperationService::uniqueCopyName("My.Folder", true, {"My.Folder"}),
              "My.Folder - Copy");
}

TEST(FileOperationServiceTest, UniqueCopyNameChainsOnAnAlreadyCopiedName)
{
    // Copying a copy, Explorer's behaviour: the suffix stacks rather than being
    // parsed back off.
    EXPECT_EQ(
        FileOperationService::uniqueCopyName("report - Copy.pdf", false, {"report - Copy.pdf"}),
        "report - Copy - Copy.pdf");
}

TEST(FileOperationServiceTest, UniqueMoveNameLeavesAFreeNameAlone)
{
    EXPECT_EQ(FileOperationService::uniqueMoveName("report.pdf", false, {"other.pdf"}),
              "report.pdf");
}

TEST(FileOperationServiceTest, UniqueMoveNameNumbersFromTwoAndSkipsTheExtension)
{
    // A move is not a duplication, so the suffix is a plain counter rather than
    // uniqueCopyName's " - Copy" (SPEC_NAME_CONFLICT_COPY_MOVE 3-4).
    EXPECT_EQ(FileOperationService::uniqueMoveName("report.pdf", false, {"report.pdf"}),
              "report (2).pdf");
    EXPECT_EQ(
        FileOperationService::uniqueMoveName("report.pdf", false, {"report.pdf", "report (2).pdf"}),
        "report (3).pdf");
}

TEST(FileOperationServiceTest, UniqueMoveNameNeverSplitsAFolderNameAtADot)
{
    EXPECT_EQ(FileOperationService::uniqueMoveName("My.Folder", true, {"My.Folder"}),
              "My.Folder (2)");
}
