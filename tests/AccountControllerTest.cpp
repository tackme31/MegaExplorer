#include "qml/AccountController.h"

#include "core/AccountPlan.h"
#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"
#include "TestApp.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>
#include <utility>

// Trap worth knowing, same shape as UploadControllerTest's note about
// checkUpload: Result<T>::success defaults to *false*, so an un-stubbed
// currentAccountIdentity() reports failure and the whole profile load bails
// out silently. SetUp() therefore installs a blanket identity expectation and
// individual tests override it only when they care.
namespace
{

class AccountControllerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        AccountIdentity identity;
        identity.email = "ada@example.com";
        identity.avatarColor = "#FF6A19";
        identity.userHandle = 42;
        ON_CALL(*mockClient, currentAccountIdentity())
            .WillByDefault(::testing::Return(Result<AccountIdentity>::ok(identity)));
        EXPECT_CALL(*mockClient, currentAccountIdentity()).Times(::testing::AnyNumber());

        // Names and avatar are irrelevant to most tests; default them to
        // "nothing set", the common real-world case.
        ON_CALL(*mockClient, getMyUserAttribute(::testing::_, ::testing::_))
            .WillByDefault(::testing::InvokeArgument<1>(
                Result<std::string>::fail("unset", MegaErrorCode::kENoEnt)));
        EXPECT_CALL(*mockClient, getMyUserAttribute(::testing::_, ::testing::_))
            .Times(::testing::AnyNumber());
        ON_CALL(*mockClient, getMyAvatar(::testing::_, ::testing::_))
            .WillByDefault(::testing::InvokeArgument<1>(
                Result<std::string>::fail("Not found", MegaErrorCode::kENoEnt)));
        EXPECT_CALL(*mockClient, getMyAvatar(::testing::_, ::testing::_))
            .Times(::testing::AnyNumber());
    }

    // Storage reads succeed with the given numbers, as many times as asked.
    void allowStorage(std::uint64_t used, std::uint64_t max, int plan = AccountPlan::kProI)
    {
        AccountInfo info;
        info.storageUsedBytes = used;
        info.storageMaxBytes = max;
        info.proLevel = plan;
        ON_CALL(*mockClient, getAccountInfo(::testing::_))
            .WillByDefault(::testing::InvokeArgument<0>(Result<AccountInfo>::ok(info)));
        EXPECT_CALL(*mockClient, getAccountInfo(::testing::_)).Times(::testing::AnyNumber());
    }

    std::shared_ptr<MockMegaClient> mockClient = std::make_shared<MockMegaClient>();
    std::shared_ptr<AccountService> service = std::make_shared<AccountService>(mockClient);
};

} // namespace

TEST_F(AccountControllerTest, RefreshPublishesEmailAndAvatarColorSynchronously)
{
    // Arrange
    allowStorage(1000, 2000);
    AccountController controller(service);

    // Act
    controller.refresh();

    // Assert -- identity is a local read, so it is available before any event
    // loop turn.
    EXPECT_EQ(controller.email(), QStringLiteral("ada@example.com"));
    EXPECT_EQ(controller.avatarColor(), QStringLiteral("#FF6A19"));
}

TEST_F(AccountControllerTest, AvatarInitialFallsBackToEmailWhenNoDisplayName)
{
    // Arrange
    allowStorage(1000, 2000);
    AccountController controller(service);

    // Act
    controller.refresh();
    flushQueuedEvents();

    // Assert
    EXPECT_TRUE(controller.displayName().isEmpty());
    EXPECT_EQ(controller.avatarInitial(), QStringLiteral("A"));
}

TEST_F(AccountControllerTest, AvatarInitialPrefersDisplayNameAndUppercases)
{
    // Arrange
    allowStorage(1000, 2000);
    EXPECT_CALL(*mockClient, getMyUserAttribute(UserAttribute::FirstName, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::string>::ok("grace")));
    EXPECT_CALL(*mockClient, getMyUserAttribute(UserAttribute::LastName, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::string>::ok("Hopper")));
    AccountController controller(service);

    // Act
    controller.refresh();
    flushQueuedEvents();

    // Assert
    EXPECT_EQ(controller.displayName(), QStringLiteral("grace Hopper"));
    EXPECT_EQ(controller.avatarInitial(), QStringLiteral("G"));
}

TEST_F(AccountControllerTest, AvatarUrlStaysEmptyWhenAccountHasNoAvatar)
{
    // Arrange -- the SetUp default is "no avatar", the common case.
    allowStorage(1000, 2000);
    AccountController controller(service);

    // Act
    controller.refresh();
    flushQueuedEvents();

    // Assert
    EXPECT_TRUE(controller.avatarUrl().isEmpty());
}

TEST_F(AccountControllerTest, StorageIsLoadedOnSuccess)
{
    // Arrange
    allowStorage(12ULL * 1024 * 1024 * 1024, 20ULL * 1024 * 1024 * 1024, AccountPlan::kProII);
    AccountController controller(service);

    // Act
    controller.refresh();
    flushQueuedEvents();

    // Assert
    EXPECT_EQ(controller.storageState(), AccountController::Loaded);
    EXPECT_NEAR(controller.storageRatio(), 0.6, 0.001);
    EXPECT_FALSE(controller.storageText().isEmpty());
    EXPECT_EQ(controller.planLevel(), AccountPlan::kProII);
}

TEST_F(AccountControllerTest, StorageIsFailedWhenFirstReadFails)
{
    // Arrange
    EXPECT_CALL(*mockClient, getAccountInfo(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(
            Result<AccountInfo>::fail("Rate limited", MegaErrorCode::kETooMany)));
    AccountController controller(service);

    // Act
    controller.refresh();
    flushQueuedEvents();

    // Assert
    EXPECT_EQ(controller.storageState(), AccountController::Failed);
    EXPECT_TRUE(controller.storageText().isEmpty());
}

TEST_F(AccountControllerTest, StorageTextIsEmptyUntilLoaded)
{
    // Arrange -- hold the callback so the read stays in flight.
    std::function<void(Result<AccountInfo>)> pending;
    EXPECT_CALL(*mockClient, getAccountInfo(::testing::_))
        .WillOnce(::testing::SaveArg<0>(&pending));
    AccountController controller(service);

    // Act
    controller.refresh();
    flushQueuedEvents();

    // Assert
    EXPECT_EQ(controller.storageState(), AccountController::Loading);
    EXPECT_TRUE(controller.storageText().isEmpty());
}

TEST_F(AccountControllerTest, SecondRefreshKeepsShowingPreviousValue)
{
    // Arrange -- pins the stale-while-revalidate rule: reopening the menu must
    // not drop the bar back to its empty loading state.
    AccountInfo info;
    info.storageUsedBytes = 1000;
    info.storageMaxBytes = 2000;
    info.proLevel = AccountPlan::kFree;
    std::function<void(Result<AccountInfo>)> pending;
    EXPECT_CALL(*mockClient, getAccountInfo(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(Result<AccountInfo>::ok(info)))
        .WillOnce(::testing::SaveArg<0>(&pending));
    AccountController controller(service);

    controller.refresh();
    flushQueuedEvents();
    ASSERT_EQ(controller.storageState(), AccountController::Loaded);

    // Act -- second open, result not back yet.
    controller.refresh();
    flushQueuedEvents();

    // Assert
    EXPECT_EQ(controller.storageState(), AccountController::Loaded);
    EXPECT_NEAR(controller.storageRatio(), 0.5, 0.001);
}

TEST_F(AccountControllerTest, FailedRefreshKeepsPreviousValue)
{
    // Arrange -- replacing good numbers with an error would be worse than
    // showing something slightly stale.
    AccountInfo info;
    info.storageUsedBytes = 1000;
    info.storageMaxBytes = 2000;
    EXPECT_CALL(*mockClient, getAccountInfo(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(Result<AccountInfo>::ok(info)))
        .WillOnce(::testing::InvokeArgument<0>(
            Result<AccountInfo>::fail("Network error", MegaErrorCode::kEAgain)));
    AccountController controller(service);

    controller.refresh();
    flushQueuedEvents();

    // Act
    controller.refresh();
    flushQueuedEvents();

    // Assert
    EXPECT_EQ(controller.storageState(), AccountController::Loaded);
    EXPECT_NEAR(controller.storageRatio(), 0.5, 0.001);
}

TEST_F(AccountControllerTest, RefreshDoesNotRefetchProfile)
{
    // Arrange -- the avatar and the name can't change mid-session, so only
    // storage is re-read.
    allowStorage(1000, 2000);
    EXPECT_CALL(*mockClient, getMyAvatar(::testing::_, ::testing::_)).Times(1);
    EXPECT_CALL(*mockClient, getMyUserAttribute(UserAttribute::FirstName, ::testing::_)).Times(1);
    AccountController controller(service);

    // Act
    controller.refresh();
    flushQueuedEvents();
    controller.refresh();
    flushQueuedEvents();

    // Assert -- covered by the .Times() expectations above.
    EXPECT_EQ(controller.storageState(), AccountController::Loaded);
}

TEST_F(AccountControllerTest, ConcurrentRefreshDoesNotIssueASecondRead)
{
    // Arrange -- reopening the menu while a read is outstanding, or
    // double-clicking retry, must not stack requests.
    std::function<void(Result<AccountInfo>)> pending;
    EXPECT_CALL(*mockClient, getAccountInfo(::testing::_))
        .Times(1)
        .WillOnce(::testing::SaveArg<0>(&pending));
    AccountController controller(service);

    // Act
    controller.refresh();
    controller.refresh();
    controller.retryAccountInfo();
    flushQueuedEvents();

    // Assert -- covered by .Times(1).
    EXPECT_EQ(controller.storageState(), AccountController::Loading);
}

TEST_F(AccountControllerTest, RetryReissuesOnlyTheAccountInfoRequest)
{
    // Arrange
    EXPECT_CALL(*mockClient, getAccountInfo(::testing::_))
        .Times(2)
        .WillRepeatedly(::testing::InvokeArgument<0>(
            Result<AccountInfo>::fail("Rate limited", MegaErrorCode::kETooMany)));
    EXPECT_CALL(*mockClient, getMyAvatar(::testing::_, ::testing::_)).Times(1);
    AccountController controller(service);

    controller.refresh();
    flushQueuedEvents();
    ASSERT_EQ(controller.storageState(), AccountController::Failed);

    // Act
    controller.retryAccountInfo();
    flushQueuedEvents();

    // Assert -- covered by the .Times() expectations above.
    EXPECT_EQ(controller.storageState(), AccountController::Failed);
}

TEST_F(AccountControllerTest, StorageRatioIsZeroWhenMaxIsZero)
{
    // Arrange -- Business / Pro Flexi accounts can report an unknown maximum.
    allowStorage(5000, 0, AccountPlan::kBusiness);
    AccountController controller(service);

    // Act
    controller.refresh();
    flushQueuedEvents();

    // Assert
    EXPECT_EQ(controller.storageState(), AccountController::Loaded);
    EXPECT_DOUBLE_EQ(controller.storageRatio(), 0.0);
    // The used figure still renders; only the "/ max" half is dropped.
    EXPECT_FALSE(controller.storageText().contains(QStringLiteral("/")));
}

TEST_F(AccountControllerTest, ResetClearsEverything)
{
    // Arrange
    allowStorage(1000, 2000);
    AccountController controller(service);
    controller.refresh();
    flushQueuedEvents();
    ASSERT_FALSE(controller.email().isEmpty());

    // Act
    controller.reset();

    // Assert
    EXPECT_TRUE(controller.email().isEmpty());
    EXPECT_TRUE(controller.displayName().isEmpty());
    EXPECT_TRUE(controller.avatarColor().isEmpty());
    EXPECT_TRUE(controller.avatarInitial().isEmpty());
    EXPECT_TRUE(controller.avatarUrl().isEmpty());
    EXPECT_EQ(controller.storageState(), AccountController::Loading);
    EXPECT_EQ(controller.planLevel(), AccountController::Unknown);
    EXPECT_DOUBLE_EQ(controller.storageRatio(), 0.0);
}

TEST_F(AccountControllerTest, LateCallbackAfterResetIsIgnored)
{
    // Arrange -- the generation guard. Without it, a logout mid-fetch lets the
    // previous account's numbers land in the next session's UI.
    std::function<void(Result<AccountInfo>)> pending;
    EXPECT_CALL(*mockClient, getAccountInfo(::testing::_))
        .WillOnce(::testing::SaveArg<0>(&pending));
    AccountController controller(service);
    int storageSignals = 0;

    controller.refresh();
    flushQueuedEvents();
    controller.reset();
    QObject::connect(&controller, &AccountController::storageChanged, &controller, [&]() {
        ++storageSignals;
    });

    // Act -- the abandoned request finally answers.
    AccountInfo info;
    info.storageUsedBytes = 999;
    info.storageMaxBytes = 1000;
    pending(Result<AccountInfo>::ok(info));
    flushQueuedEvents();

    // Assert
    EXPECT_EQ(storageSignals, 0);
    EXPECT_EQ(controller.storageState(), AccountController::Loading);
    EXPECT_DOUBLE_EQ(controller.storageRatio(), 0.0);
}

TEST_F(AccountControllerTest, RefreshAfterResetReloadsProfile)
{
    // Arrange -- signing into another account must re-read everything.
    allowStorage(1000, 2000);
    EXPECT_CALL(*mockClient, getMyAvatar(::testing::_, ::testing::_)).Times(2);
    AccountController controller(service);

    controller.refresh();
    flushQueuedEvents();
    controller.reset();

    // Act
    controller.refresh();
    flushQueuedEvents();

    // Assert
    EXPECT_EQ(controller.email(), QStringLiteral("ada@example.com"));
}

TEST_F(AccountControllerTest, FileVersioningIsEnabledBeforeAnythingIsRead)
{
    // Arrange & Act -- nothing is fetched at construction.
    AccountController controller(service);

    // Assert -- the conflict dialog can word itself from this the moment it opens.
    EXPECT_TRUE(controller.fileVersioningEnabled());
}

TEST_F(AccountControllerTest, LoadFileVersioningPublishesDisabled)
{
    // Arrange
    EXPECT_CALL(*mockClient, getFileVersioningEnabled(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(Result<bool>::ok(false)));
    AccountController controller(service);
    int changes = 0;
    QObject::connect(&controller, &AccountController::fileVersioningEnabledChanged, [&] {
        ++changes;
    });

    // Act
    controller.loadFileVersioning();
    flushQueuedEvents();

    // Assert
    EXPECT_FALSE(controller.fileVersioningEnabled());
    EXPECT_EQ(changes, 1);
}

TEST_F(AccountControllerTest, ResetRestoresFileVersioningDefault)
{
    // Arrange -- the setting belongs to the account that was signed in, so the next
    // one must not inherit the previous account's warning.
    EXPECT_CALL(*mockClient, getFileVersioningEnabled(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(Result<bool>::ok(false)));
    AccountController controller(service);
    controller.loadFileVersioning();
    flushQueuedEvents();

    // Act
    controller.reset();

    // Assert
    EXPECT_TRUE(controller.fileVersioningEnabled());
}

// A read abandoned by a logout can still land afterwards; the generation counter
// keeps it from re-disabling versioning under the account that signed in next.
TEST_F(AccountControllerTest, FileVersioningAnswerAfterResetIsDropped)
{
    // Arrange
    std::function<void(Result<bool>)> pending;
    EXPECT_CALL(*mockClient, getFileVersioningEnabled(::testing::_))
        .WillOnce(::testing::SaveArg<0>(&pending));
    AccountController controller(service);
    controller.loadFileVersioning();

    // Act
    controller.reset();
    pending(Result<bool>::ok(false));
    flushQueuedEvents();

    // Assert
    EXPECT_TRUE(controller.fileVersioningEnabled());
}
