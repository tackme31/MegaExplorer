#include "core/AuthService.h"

#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"
#include "MockSessionStore.h"

#include <cstdint>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <utility>
#include <vector>

namespace
{

// AuthService::FetchProgressCallback has no default: it is handed straight to
// IMegaClient::fetchNodes, which invokes it unconditionally, so an empty
// std::function would crash mid-login. Tests that don't assert on progress
// pass this explicit no-op.
AuthService::FetchProgressCallback noProgress()
{
    return [](std::uint64_t, std::uint64_t) {};
}

} // namespace

TEST(AuthServiceTest, RestoreSessionWithNothingStoredSkipsLoginWithSession)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockSessionStore = std::make_shared<MockSessionStore>();

    EXPECT_CALL(*mockSessionStore, loadSession())
        .WillOnce(::testing::Return(Result<std::string>::ok("")));
    EXPECT_CALL(*mockClient, loginWithSession(::testing::_, ::testing::_)).Times(0);

    AuthService service(mockClient, mockSessionStore);
    Result<void> captured;

    // Act
    service.restoreSession(noProgress(), [&](Result<void> result) {
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

    EXPECT_CALL(*mockSessionStore, loadSession())
        .WillOnce(::testing::Return(Result<std::string>::ok("stored-token")));
    EXPECT_CALL(*mockClient, loginWithSession("stored-token", ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, currentSessionToken())
        .WillOnce(::testing::Return(Result<std::string>::ok("stored-token")));
    EXPECT_CALL(*mockSessionStore, saveSession("stored-token"))
        .WillOnce(::testing::Return(Result<void>::ok()));

    AuthService service(mockClient, mockSessionStore);
    Result<void> captured;

    // Act
    service.restoreSession(noProgress(), [&](Result<void> result) {
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

    EXPECT_CALL(*mockSessionStore, loadSession())
        .WillOnce(::testing::Return(Result<std::string>::ok("stored-token")));
    EXPECT_CALL(*mockClient, loginWithSession("stored-token", ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(
            Result<void>::fail("invalid session", MegaErrorCode::kESid)));
    EXPECT_CALL(*mockSessionStore, clearSession()).WillOnce(::testing::Return(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_, ::testing::_)).Times(0);

    AuthService service(mockClient, mockSessionStore);
    Result<void> captured;

    // Act
    service.restoreSession(noProgress(), [&](Result<void> result) {
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

    EXPECT_CALL(*mockSessionStore, loadSession())
        .WillOnce(::testing::Return(Result<std::string>::ok("stored-token")));
    EXPECT_CALL(*mockClient, loginWithSession("stored-token", ::testing::_))
        .WillOnce(
            ::testing::InvokeArgument<1>(Result<void>::fail("offline", MegaErrorCode::kEAgain)));
    EXPECT_CALL(*mockSessionStore, clearSession()).Times(0);

    AuthService service(mockClient, mockSessionStore);
    Result<void> captured;

    // Act
    service.restoreSession(noProgress(), [&](Result<void> result) {
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

    EXPECT_CALL(*mockSessionStore, loadSession())
        .WillOnce(::testing::Return(Result<std::string>::fail("decrypt failed", MegaErrorCode::kEInternal)));
    EXPECT_CALL(*mockSessionStore, clearSession()).WillOnce(::testing::Return(Result<void>::ok()));
    EXPECT_CALL(*mockClient, loginWithSession(::testing::_, ::testing::_)).Times(0);

    AuthService service(mockClient, mockSessionStore);
    Result<void> captured;

    // Act
    service.restoreSession(noProgress(), [&](Result<void> result) {
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

    EXPECT_CALL(*mockClient, login("user@example.com", "pw", ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<void>::ok()));

    ::testing::Sequence seq;
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_, ::testing::_))
        .InSequence(seq)
        .WillOnce(::testing::InvokeArgument<1>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, currentSessionToken())
        .InSequence(seq)
        .WillOnce(::testing::Return(Result<std::string>::ok("token123")));
    EXPECT_CALL(*mockSessionStore, saveSession("token123"))
        .InSequence(seq)
        .WillOnce(::testing::Return(Result<void>::ok()));

    AuthService service(mockClient, mockSessionStore);
    Result<void> captured;

    // Act
    service.login("user@example.com", "pw", noProgress(), [&](Result<void> result) {
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

    EXPECT_CALL(*mockClient, login(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(
            Result<void>::fail("network error", MegaErrorCode::kEAgain)));
    EXPECT_CALL(*mockSessionStore, saveSession(::testing::_)).Times(0);

    AuthService service(mockClient, mockSessionStore);
    Result<void> captured;

    // Act
    service.login("e", "p", noProgress(), [&](Result<void> result) {
        captured = std::move(result);
    });

    // Assert
    EXPECT_FALSE(captured.success);
}

TEST(AuthServiceTest, LoginForwardsFetchNodesProgressToCallerUntouched)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockSessionStore = std::make_shared<MockSessionStore>();

    EXPECT_CALL(*mockClient, login("e", "p", ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(Result<void>::ok()));
    // Stands in for MegaApi's onRequestUpdate: a couple of byte updates
    // during the fetch, then completion.
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_, ::testing::_))
        .WillOnce([](std::function<void(std::uint64_t, std::uint64_t)> onProgress,
                     std::function<void(Result<void>)> onDone) {
            onProgress(0, 0); // total not known yet
            onProgress(64, 183);
            onDone(Result<void>::ok());
        });
    EXPECT_CALL(*mockClient, currentSessionToken())
        .WillOnce(::testing::Return(Result<std::string>::ok("token")));
    EXPECT_CALL(*mockSessionStore, saveSession("token"))
        .WillOnce(::testing::Return(Result<void>::ok()));

    AuthService service(mockClient, mockSessionStore);
    std::vector<std::pair<std::uint64_t, std::uint64_t>> progress;

    // Act
    service.login(
        "e",
        "p",
        [&](std::uint64_t transferredBytes, std::uint64_t totalBytes) {
            progress.emplace_back(transferredBytes, totalBytes);
        },
        [](Result<void>) {});

    // Assert -- AuthService adds nothing of its own to the progress stream.
    ASSERT_EQ(progress.size(), 2u);
    EXPECT_EQ(progress[0], std::make_pair(std::uint64_t{0}, std::uint64_t{0}));
    EXPECT_EQ(progress[1], std::make_pair(std::uint64_t{64}, std::uint64_t{183}));
}

TEST(AuthServiceTest, LoginMfaRequiredPropagatesErrorCodeUnmodified)
{
    // Arrange
    auto mockClient = std::make_shared<MockMegaClient>();
    auto mockSessionStore = std::make_shared<MockSessionStore>();

    EXPECT_CALL(*mockClient, login(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<2>(
            Result<void>::fail("2fa required", MegaErrorCode::kEMfaRequired)));
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_, ::testing::_)).Times(0);

    AuthService service(mockClient, mockSessionStore);
    Result<void> captured;

    // Act
    service.login("e", "p", noProgress(), [&](Result<void> result) {
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
    ON_CALL(*mockSessionStore, saveSession(::testing::_))
        .WillByDefault(::testing::Return(Result<void>::ok()));

    EXPECT_CALL(*mockClient, multiFactorAuthLogin("e", "p", "123456", ::testing::_))
        .WillOnce(::testing::InvokeArgument<3>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_, ::testing::_))
        .WillOnce(::testing::InvokeArgument<1>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, currentSessionToken())
        .WillOnce(::testing::Return(Result<std::string>::ok("token")));

    AuthService service(mockClient, mockSessionStore);
    Result<void> captured;

    // Act
    service.loginWithTwoFactor("e", "p", "123456", noProgress(), [&](Result<void> result) {
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

    EXPECT_CALL(*mockClient,
                multiFactorAuthLogin(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(
            ::testing::InvokeArgument<3>(Result<void>::fail("bad pin", MegaErrorCode::kENoEnt)));
    EXPECT_CALL(*mockClient, fetchNodes(::testing::_, ::testing::_)).Times(0);

    AuthService service(mockClient, mockSessionStore);
    Result<void> captured;

    // Act
    service.loginWithTwoFactor("e", "p", "000000", noProgress(), [&](Result<void> result) {
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

    EXPECT_CALL(*mockClient, logout(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(Result<void>::ok()));
    EXPECT_CALL(*mockSessionStore, clearSession()).WillOnce(::testing::Return(Result<void>::ok()));

    AuthService service(mockClient, mockSessionStore);
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

    EXPECT_CALL(*mockClient, logout(::testing::_))
        .WillOnce(::testing::InvokeArgument<0>(
            Result<void>::fail("network error", MegaErrorCode::kEAgain)));
    EXPECT_CALL(*mockSessionStore, clearSession()).WillOnce(::testing::Return(Result<void>::ok()));

    AuthService service(mockClient, mockSessionStore);
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
