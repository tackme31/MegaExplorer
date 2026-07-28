#include "core/AuthService.h"

#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"
#include "MockNodeCache.h"
#include "MockSessionStore.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

TEST(AuthServiceTest, RestoreSessionWithNothingStoredSkipsLoginWithSession)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockSessionStore = std::make_shared<MockSessionStore>();
    auto mockNodeCache = std::make_shared<MockNodeCache>();

    EXPECT_CALL(*mockSessionStore, loadSession())
        .WillOnce(::testing::Return(Result<std::string>::ok("")));
    EXPECT_CALL(*mockClient, loginWithSession(::testing::_, ::testing::_)).Times(0);

    AuthService service(mockClient, mockSessionStore, mockNodeCache);
    Result<void> captured;

    // Act
    service.restoreSession([&](Result<void> result) {
        captured = std::move(result);
    });

    // Assert
    EXPECT_FALSE(captured.success);
    EXPECT_EQ(captured.errorCode, kNoStoredSession);
}

TEST(AuthServiceTest, RestoreSessionWithStoredTokenSucceedsAndSavesToken)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockSessionStore = std::make_shared<MockSessionStore>();
    auto mockNodeCache = std::make_shared<MockNodeCache>();

    EXPECT_CALL(*mockSessionStore, loadSession())
        .WillOnce(::testing::Return(Result<std::string>::ok("stored-token")));
    EXPECT_CALL(*mockClient, loginWithSession("stored-token", ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, currentSessionToken())
        .WillOnce(::testing::Return(Result<std::string>::ok("stored-token")));
    EXPECT_CALL(*mockSessionStore, saveSession("stored-token"))
        .WillOnce(::testing::Return(Result<void>::ok()));

    AuthService service(mockClient, mockSessionStore, mockNodeCache);
    Result<void> captured;

    // Act
    service.restoreSession([&](Result<void> result) {
        captured = std::move(result);
    });

    // Assert
    EXPECT_TRUE(captured.success);
}

TEST(AuthServiceTest, RestoreSessionDefinitivelyInvalidClearsStoredSession)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockSessionStore = std::make_shared<MockSessionStore>();
    auto mockNodeCache = std::make_shared<MockNodeCache>();

    EXPECT_CALL(*mockSessionStore, loadSession())
        .WillOnce(::testing::Return(Result<std::string>::ok("stored-token")));
    EXPECT_CALL(*mockClient, loginWithSession("stored-token", ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(
            Result<void>::fail("invalid session", MegaErrorCode::kESid)));
    EXPECT_CALL(*mockSessionStore, clearSession()).WillOnce(::testing::Return(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_)).Times(0);

    AuthService service(mockClient, mockSessionStore, mockNodeCache);
    Result<void> captured;

    // Act
    service.restoreSession([&](Result<void> result) {
        captured = std::move(result);
    });

    // Assert
    EXPECT_FALSE(captured.success);
    EXPECT_EQ(captured.errorCode, MegaErrorCode::kESid);
}

TEST(AuthServiceTest, RestoreSessionTransientFailureLeavesStoredSessionUntouched)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockSessionStore = std::make_shared<MockSessionStore>();
    auto mockNodeCache = std::make_shared<MockNodeCache>();

    EXPECT_CALL(*mockSessionStore, loadSession())
        .WillOnce(::testing::Return(Result<std::string>::ok("stored-token")));
    EXPECT_CALL(*mockClient, loginWithSession("stored-token", ::testing::_))
        .WillOnce(
            ::testing::InvokeArgument<1>(Result<void>::fail("offline", MegaErrorCode::kEAgain)));
    EXPECT_CALL(*mockSessionStore, clearSession()).Times(0);

    AuthService service(mockClient, mockSessionStore, mockNodeCache);
    Result<void> captured;

    // Act
    service.restoreSession([&](Result<void> result) {
        captured = std::move(result);
    });

    // Assert
    EXPECT_FALSE(captured.success);
    EXPECT_EQ(captured.errorCode, MegaErrorCode::kEAgain);
}

TEST(AuthServiceTest, RestoreSessionCorruptSessionFileSelfHealsByClearing)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockSessionStore = std::make_shared<MockSessionStore>();
    auto mockNodeCache = std::make_shared<MockNodeCache>();

    EXPECT_CALL(*mockSessionStore, loadSession())
        .WillOnce(::testing::Return(Result<std::string>::fail("decrypt failed")));
    EXPECT_CALL(*mockSessionStore, clearSession()).WillOnce(::testing::Return(Result<void>::ok()));
    EXPECT_CALL(*mockClient, loginWithSession(::testing::_, ::testing::_)).Times(0);

    AuthService service(mockClient, mockSessionStore, mockNodeCache);
    Result<void> captured;

    // Act
    service.restoreSession([&](Result<void> result) {
        captured = std::move(result);
    });

    // Assert
    EXPECT_FALSE(captured.success);
}

TEST(AuthServiceTest, LoginSuccessCallsFetchNodesThenCurrentSessionTokenThenSaveSession)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockSessionStore = std::make_shared<MockSessionStore>();
    auto mockNodeCache = std::make_shared<MockNodeCache>();

    EXPECT_CALL(*mockClient, login("user@example.com", "pw", ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<void>::ok()));

    ::testing::Sequence seq;
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_))
        .InSequence(seq)
        .WillOnce(::testing::InvokeArgument<0>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, currentSessionToken())
        .InSequence(seq)
        .WillOnce(::testing::Return(Result<std::string>::ok("token123")));
    EXPECT_CALL(*mockSessionStore, saveSession("token123"))
        .InSequence(seq)
        .WillOnce(::testing::Return(Result<void>::ok()));

    AuthService service(mockClient, mockSessionStore, mockNodeCache);
    Result<void> captured;

    // Act
    service.login("user@example.com", "pw", [&](Result<void> result) {
        captured = std::move(result);
    });

    // Assert
    EXPECT_TRUE(captured.success);
}

TEST(AuthServiceTest, LoginFetchNodesFailureSkipsSaveSession)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockSessionStore = std::make_shared<MockSessionStore>();
    auto mockNodeCache = std::make_shared<MockNodeCache>();

    EXPECT_CALL(*mockClient, login(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(
            Result<void>::fail("network error", MegaErrorCode::kEAgain)));
    EXPECT_CALL(*mockSessionStore, saveSession(::testing::_)).Times(0);

    AuthService service(mockClient, mockSessionStore, mockNodeCache);
    Result<void> captured;

    // Act
    service.login("e", "p", [&](Result<void> result) {
        captured = std::move(result);
    });

    // Assert
    EXPECT_FALSE(captured.success);
}

TEST(AuthServiceTest, LoginMfaRequiredPropagatesErrorCodeUnmodified)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockSessionStore = std::make_shared<MockSessionStore>();
    auto mockNodeCache = std::make_shared<MockNodeCache>();

    EXPECT_CALL(*mockClient, login(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<void>::fail("2fa required", MegaErrorCode::kEMfaRequired)));
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_)).Times(0);

    AuthService service(mockClient, mockSessionStore, mockNodeCache);
    Result<void> captured;

    // Act
    service.login("e", "p", [&](Result<void> result) {
        captured = std::move(result);
    });

    // Assert
    EXPECT_FALSE(captured.success);
    EXPECT_EQ(captured.errorCode, MegaErrorCode::kEMfaRequired);
}

TEST(AuthServiceTest, LoginWithTwoFactorSuccessCompletesLikeLogin)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockSessionStore = std::make_shared<::testing::NiceMock<MockSessionStore>>();
    auto mockNodeCache = std::make_shared<MockNodeCache>();
    ON_CALL(*mockSessionStore, saveSession(::testing::_))
        .WillByDefault(::testing::Return(Result<void>::ok()));

    EXPECT_CALL(*mockClient, multiFactorAuthLogin("e", "p", "123456", ::testing::_))
        .WillOnce(::testing::InvokeArgument<3>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, currentSessionToken())
        .WillOnce(::testing::Return(Result<std::string>::ok("token")));

    AuthService service(mockClient, mockSessionStore, mockNodeCache);
    Result<void> captured;

    // Act
    service.loginWithTwoFactor("e", "p", "123456", [&](Result<void> result) {
        captured = std::move(result);
    });

    // Assert
    EXPECT_TRUE(captured.success);
}

TEST(AuthServiceTest, LoginWithTwoFactorFailurePropagates)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockSessionStore = std::make_shared<MockSessionStore>();
    auto mockNodeCache = std::make_shared<MockNodeCache>();

    EXPECT_CALL(*mockClient,
                multiFactorAuthLogin(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(
            ::testing::InvokeArgument<3>(Result<void>::fail("bad pin", MegaErrorCode::kENoEnt)));
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_)).Times(0);

    AuthService service(mockClient, mockSessionStore, mockNodeCache);
    Result<void> captured;

    // Act
    service.loginWithTwoFactor("e", "p", "000000", [&](Result<void> result) {
        captured = std::move(result);
    });

    // Assert
    EXPECT_FALSE(captured.success);
    EXPECT_EQ(captured.errorCode, MegaErrorCode::kENoEnt);
}

TEST(AuthServiceTest, LogoutClearsStateWhenClientLogoutSucceeds)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockSessionStore = std::make_shared<MockSessionStore>();
    auto mockNodeCache = std::make_shared<MockNodeCache>();

    EXPECT_CALL(*mockClient, logout(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(Result<void>::ok()));
    EXPECT_CALL(*mockSessionStore, clearSession()).WillOnce(::testing::Return(Result<void>::ok()));
    EXPECT_CALL(*mockNodeCache, clearAll()).WillOnce(::testing::Return(Result<void>::ok()));

    AuthService service(mockClient, mockSessionStore, mockNodeCache);
    Result<void> captured;

    // Act
    service.logout([&](Result<void> result) {
        captured = std::move(result);
    });

    // Assert
    EXPECT_TRUE(captured.success);
}

TEST(AuthServiceTest, LogoutClearsStateEvenWhenClientLogoutFails)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockSessionStore = std::make_shared<MockSessionStore>();
    auto mockNodeCache = std::make_shared<MockNodeCache>();

    EXPECT_CALL(*mockClient, logout(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(
            Result<void>::fail("network error", MegaErrorCode::kEAgain)));
    EXPECT_CALL(*mockSessionStore, clearSession()).WillOnce(::testing::Return(Result<void>::ok()));
    EXPECT_CALL(*mockNodeCache, clearAll()).WillOnce(::testing::Return(Result<void>::ok()));

    AuthService service(mockClient, mockSessionStore, mockNodeCache);
    Result<void> captured;

    // Act
    service.logout([&](Result<void> result) {
        captured = std::move(result);
    });

    // Assert: logout is treated as always-successful from the caller's
    // perspective, regardless of the SDK round-trip's own outcome.
    EXPECT_TRUE(captured.success);
}

TEST(AuthServiceTest, IsSessionDefinitivelyInvalidClassifiesKnownCodes)
{
    EXPECT_TRUE(isSessionDefinitivelyInvalid(MegaErrorCode::kEArgs));
    EXPECT_TRUE(isSessionDefinitivelyInvalid(MegaErrorCode::kEExpired));
    EXPECT_TRUE(isSessionDefinitivelyInvalid(MegaErrorCode::kENoEnt));
    EXPECT_TRUE(isSessionDefinitivelyInvalid(MegaErrorCode::kEAccess));
    EXPECT_TRUE(isSessionDefinitivelyInvalid(MegaErrorCode::kESid));
    EXPECT_TRUE(isSessionDefinitivelyInvalid(MegaErrorCode::kEBlocked));

    EXPECT_FALSE(isSessionDefinitivelyInvalid(MegaErrorCode::kEAgain));
    EXPECT_FALSE(isSessionDefinitivelyInvalid(MegaErrorCode::kEFailed));
    EXPECT_FALSE(isSessionDefinitivelyInvalid(MegaErrorCode::kETooMany));
    EXPECT_FALSE(isSessionDefinitivelyInvalid(MegaErrorCode::kEMfaRequired));
    EXPECT_FALSE(isSessionDefinitivelyInvalid(9999)); // unknown code -> treated as transient
}
