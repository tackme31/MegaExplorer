#include "core/AccountService.h"

#include "core/AccountPlan.h"
#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <string>
#include <utility>

namespace
{

// Both name attributes resolve successfully with the given values. Most
// display-name tests only care about the join, not about how the two reads
// were issued.
void expectNames(MockMegaClient& client, const std::string& first, const std::string& last)
{
    EXPECT_CALL(client, getMyUserAttribute(UserAttribute::FirstName, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::string>::ok(first)));
    EXPECT_CALL(client, getMyUserAttribute(UserAttribute::LastName, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::string>::ok(last)));
}

} // namespace

TEST(AccountServiceTest, IdentityPassesThroughClientResult)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    AccountIdentity identity;
    identity.email = "ada@example.com";
    identity.avatarColor = "#FF6A19";
    identity.userHandle = 42;
    EXPECT_CALL(*mockClient, currentAccountIdentity())
        .WillOnce(::testing::Return(Result<AccountIdentity>::ok(identity)));

    AccountService service(mockClient);

    // Act
    const Result<AccountIdentity> result = service.identity();

    // Assert
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.value(), identity);
}

TEST(AccountServiceTest, IdentityPropagatesFailureWhenNotLoggedIn)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, currentAccountIdentity())
        .WillOnce(::testing::Return(Result<AccountIdentity>::fail("not logged in", MegaErrorCode::kEInternal)));

    AccountService service(mockClient);

    // Act
    const Result<AccountIdentity> result = service.identity();

    // Assert
    EXPECT_FALSE(result.success);
}

TEST(AccountServiceTest, DisplayNameJoinsFirstAndLast)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    expectNames(*mockClient, "Ada", "Lovelace");
    AccountService service(mockClient);
    std::string captured;
    int callCount = 0;

    // Act
    service.loadDisplayName([&](std::string name) {
        captured = std::move(name);
        ++callCount;
    });

    // Assert
    EXPECT_EQ(captured, "Ada Lovelace");
    EXPECT_EQ(callCount, 1);
}

TEST(AccountServiceTest, DisplayNameUsesFirstOnlyWhenLastNameFails)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getMyUserAttribute(UserAttribute::FirstName, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::string>::ok("Ada")));
    EXPECT_CALL(*mockClient, getMyUserAttribute(UserAttribute::LastName, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(
            Result<std::string>::fail("unset", MegaErrorCode::kENoEnt)));
    AccountService service(mockClient);
    std::string captured;

    // Act
    service.loadDisplayName([&](std::string name) {
        captured = std::move(name);
    });

    // Assert
    EXPECT_EQ(captured, "Ada");
}

TEST(AccountServiceTest, DisplayNameUsesLastOnlyWhenFirstNameFails)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getMyUserAttribute(UserAttribute::FirstName, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(
            Result<std::string>::fail("unset", MegaErrorCode::kENoEnt)));
    EXPECT_CALL(*mockClient, getMyUserAttribute(UserAttribute::LastName, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::string>::ok("Lovelace")));
    AccountService service(mockClient);
    std::string captured;

    // Act
    service.loadDisplayName([&](std::string name) {
        captured = std::move(name);
    });

    // Assert
    EXPECT_EQ(captured, "Lovelace");
}

TEST(AccountServiceTest, DisplayNameIsEmptyWhenBothFailAndStillReportsOnce)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getMyUserAttribute(::testing::_, ::testing::_))
        .Times(2)
        .WillRepeatedly(::testing::InvokeArgument<1>(
            Result<std::string>::fail("unset", MegaErrorCode::kENoEnt)));
    AccountService service(mockClient);
    std::string captured = "sentinel";
    int callCount = 0;

    // Act
    service.loadDisplayName([&](std::string name) {
        captured = std::move(name);
        ++callCount;
    });

    // Assert
    EXPECT_TRUE(captured.empty());
    EXPECT_EQ(callCount, 1);
}

TEST(AccountServiceTest, DisplayNameTrimsWhitespaceOnlyAttributes)
{
    // Arrange -- a whitespace-only last name must not leave a trailing space.
    auto mockClient = std::make_shared<MockMegaClient>();
    expectNames(*mockClient, "  Ada  ", "   ");
    AccountService service(mockClient);
    std::string captured;

    // Act
    service.loadDisplayName([&](std::string name) {
        captured = std::move(name);
    });

    // Assert
    EXPECT_EQ(captured, "Ada");
}

TEST(AccountServiceTest, DisplayNameIssuesRequestsSequentially)
{
    // Arrange -- pins the design decision that removes the need for a mutex:
    // the last-name read must not be issued until the first-name callback has
    // run, so no accumulator is ever touched from two threads.
    auto mockClient = std::make_shared<MockMegaClient>();
    std::function<void(Result<std::string>)> firstNameDone;
    bool lastNameRequested = false;

    EXPECT_CALL(*mockClient, getMyUserAttribute(UserAttribute::FirstName, ::testing::_))
        .WillOnce(::testing::SaveArg<1>(&firstNameDone));
    EXPECT_CALL(*mockClient, getMyUserAttribute(UserAttribute::LastName, ::testing::_))
        .WillOnce(
            ::testing::DoAll(::testing::Assign(&lastNameRequested, true),
                             ::testing::InvokeArgument<1>(Result<std::string>::ok("Lovelace"))));

    AccountService service(mockClient);
    std::string captured;

    // Act
    service.loadDisplayName([&](std::string name) {
        captured = std::move(name);
    });

    // Assert -- nothing else has been asked for yet.
    EXPECT_FALSE(lastNameRequested);

    firstNameDone(Result<std::string>::ok("Ada"));

    EXPECT_TRUE(lastNameRequested);
    EXPECT_EQ(captured, "Ada Lovelace");
}

TEST(AccountServiceTest, LoadAvatarReportsPathOnSuccess)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getMyAvatar("C:\\tmp\\42.jpg", ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<std::string>::ok("C:\\tmp\\42.jpg")));
    AccountService service(mockClient);
    Result<AvatarOutcome> captured;

    // Act
    service.loadAvatar("C:\\tmp\\42.jpg", [&](Result<AvatarOutcome> result) {
        captured = std::move(result);
    });

    // Assert
    EXPECT_TRUE(captured.success);
    EXPECT_TRUE(captured.value().hasAvatar);
    EXPECT_EQ(captured.value().localPath, "C:\\tmp\\42.jpg");
    EXPECT_EQ(captured.value().errorCode, 0);
}

TEST(AccountServiceTest, LoadAvatarReportsNoAvatarOnENoEnt)
{
    // Arrange -- the common case: the account never set an avatar. This must
    // arrive as a *success* carrying hasAvatar == false, never as a failure,
    // because there is no error UI for avatars.
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getMyAvatar(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(
            Result<std::string>::fail("Not found", MegaErrorCode::kENoEnt)));
    AccountService service(mockClient);
    Result<AvatarOutcome> captured;

    // Act
    service.loadAvatar("C:\\tmp\\42.jpg", [&](Result<AvatarOutcome> result) {
        captured = std::move(result);
    });

    // Assert
    EXPECT_TRUE(captured.success);
    EXPECT_FALSE(captured.value().hasAvatar);
    EXPECT_TRUE(captured.value().localPath.empty());
    EXPECT_EQ(captured.value().errorCode, MegaErrorCode::kENoEnt);
}

TEST(AccountServiceTest, LoadAvatarSwallowsOtherErrorsToo)
{
    // Arrange -- the swallow is deliberately not keyed on kENoEnt: megaapi.h
    // does not document which code the no-avatar case yields, and no caller
    // could act on the difference.
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getMyAvatar(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(
            Result<std::string>::fail("Network error", MegaErrorCode::kEAgain)));
    AccountService service(mockClient);
    Result<AvatarOutcome> captured;

    // Act
    service.loadAvatar("C:\\tmp\\42.jpg", [&](Result<AvatarOutcome> result) {
        captured = std::move(result);
    });

    // Assert
    EXPECT_TRUE(captured.success);
    EXPECT_FALSE(captured.value().hasAvatar);
    EXPECT_EQ(captured.value().errorCode, MegaErrorCode::kEAgain);
}

TEST(AccountServiceTest, LoadAccountInfoPassesThrough)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    AccountInfo info;
    info.storageUsedBytes = 12345;
    info.storageMaxBytes = 20000;
    info.proLevel = AccountPlan::kProI;
    EXPECT_CALL(*mockClient, getAccountInfo(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(Result<AccountInfo>::ok(info)));
    AccountService service(mockClient);
    Result<AccountInfo> captured;

    // Act
    service.loadAccountInfo([&](Result<AccountInfo> result) {
        captured = std::move(result);
    });

    // Assert
    EXPECT_TRUE(captured.success);
    EXPECT_EQ(captured.value(), info);
}

TEST(AccountServiceTest, LoadAccountInfoPropagatesFailureCode)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    EXPECT_CALL(*mockClient, getAccountInfo(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(
            Result<AccountInfo>::fail("Rate limited", MegaErrorCode::kETooMany)));
    AccountService service(mockClient);
    Result<AccountInfo> captured;

    // Act
    service.loadAccountInfo([&](Result<AccountInfo> result) {
        captured = std::move(result);
    });

    // Assert
    EXPECT_FALSE(captured.success);
    EXPECT_EQ(captured.errorCode, MegaErrorCode::kETooMany);
}
