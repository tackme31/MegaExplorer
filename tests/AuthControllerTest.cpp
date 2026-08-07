#include "qml/AuthController.h"

#include "core/MegaErrorCodes.h"
#include "MockMegaClient.h"
#include "MockSessionStore.h"
#include "TestApp.h"

#include <QEventLoop>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using ::testing::_;
using ::testing::AnyNumber;
using ::testing::DoAll;
using ::testing::InvokeArgument;
using ::testing::Return;
using ::testing::SaveArg;

// Trap, same shape as AccountControllerTest's note on currentAccountIdentity:
// Result<T>::success defaults to *false*, so an un-stubbed loadSession() or
// currentSessionToken() reads as a failure rather than as "nothing happened".
// SetUp() therefore installs blanket defaults for everything on the happy
// path, and individual tests override only what they are actually about.
//
// The stall timeout is passed to the constructor throughout: its production
// default is 8s (kDefaultStallTimeoutMs), which every timer test would
// otherwise have to wait out for real.
namespace
{

using ProgressCallback = std::function<void(std::uint64_t, std::uint64_t)>;
using DoneCallback = std::function<void(Result<void>)>;

// Progress reaches the controller through two hops -- the callback handed to
// AuthService posts to the GUI thread, and handleFetchProgress can post again
// -- so a single drain does not always suffice.
void flush()
{
    flushQueuedEvents();
    flushQueuedEvents();
}

constexpr int kStageWaitTimeoutMs = 2000;

// Deliberately not QSignalSpy/QTest::qWait: those live in Qt6::Test, which
// this target does not link (see tests/CMakeLists.txt).
bool waitForStage(AuthController& controller, AuthController::LoadingStage expected)
{
    if (controller.loadingStage() == expected)
        return true;

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(
        &controller, &AuthController::loadingStateChanged, &loop, [&controller, expected, &loop]() {
            if (controller.loadingStage() == expected)
                loop.quit();
        });
    timeout.start(kStageWaitTimeoutMs);
    loop.exec();
    return controller.loadingStage() == expected;
}

// Lets real time pass with timers running, for the cases that assert a
// transition did *not* happen.
void spinFor(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

class AuthControllerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        testApp();

        // "Nothing stored" -- the cheapest route from Restoring to LoggedOut,
        // which is the gate login() gets tested behind.
        ON_CALL(*mockSessionStore, loadSession())
            .WillByDefault(Return(Result<std::string>::ok("")));
        EXPECT_CALL(*mockSessionStore, loadSession()).Times(AnyNumber());
        ON_CALL(*mockSessionStore, saveSession(_)).WillByDefault(Return(Result<void>::ok()));
        EXPECT_CALL(*mockSessionStore, saveSession(_)).Times(AnyNumber());
        ON_CALL(*mockSessionStore, clearSession()).WillByDefault(Return(Result<void>::ok()));
        EXPECT_CALL(*mockSessionStore, clearSession()).Times(AnyNumber());

        ON_CALL(*mockClient, currentSessionToken())
            .WillByDefault(Return(Result<std::string>::ok("session-token")));
        EXPECT_CALL(*mockClient, currentSessionToken()).Times(AnyNumber());
        ON_CALL(*mockClient, logout(_)).WillByDefault(InvokeArgument<0>(Result<void>::ok()));
        EXPECT_CALL(*mockClient, logout(_)).Times(AnyNumber());
    }

    void allowLoginSuccess()
    {
        EXPECT_CALL(*mockClient, login(_, _, _))
            .WillRepeatedly(InvokeArgument<2>(Result<void>::ok()));
        EXPECT_CALL(*mockClient, fetchNodes(_, _))
            .WillRepeatedly(InvokeArgument<1>(Result<void>::ok()));
    }

    void allowLoginFailure(int errorCode, const char* message = "Server said no")
    {
        EXPECT_CALL(*mockClient, login(_, _, _))
            .WillRepeatedly(InvokeArgument<2>(Result<void>::fail(message, errorCode)));
    }

    // Captures what the controller forwards to the SDK for a 2FA attempt --
    // the only observable proof that the pending credentials are the ones the
    // matching login() supplied.
    void captureTwoFactorSubmission(int errorCode = MegaErrorCode::kENoEnt)
    {
        EXPECT_CALL(*mockClient, multiFactorAuthLogin(_, _, _, _))
            .WillRepeatedly(DoAll(SaveArg<0>(&submittedEmail),
                                  SaveArg<1>(&submittedPassword),
                                  SaveArg<2>(&submittedPin),
                                  InvokeArgument<3>(Result<void>::fail("rejected", errorCode))));
    }

    // Restoring -> LoggedOut. Valid once per controller: restoreSession() is
    // gated on Restoring.
    void arriveAtLoggedOut(AuthController& controller)
    {
        controller.restoreSession();
        flush();
        EXPECT_EQ(controller.authState(), AuthController::LoggedOut);
    }

    void arriveAtLoggedIn(AuthController& controller)
    {
        allowLoginSuccess();
        arriveAtLoggedOut(controller);
        controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
        flush();
        EXPECT_EQ(controller.authState(), AuthController::LoggedIn);
    }

    std::shared_ptr<MockMegaClient> mockClient = std::make_shared<MockMegaClient>();
    std::shared_ptr<MockSessionStore> mockSessionStore = std::make_shared<MockSessionStore>();
    std::shared_ptr<AuthService> authService =
        std::make_shared<AuthService>(mockClient, mockSessionStore);

    std::string submittedEmail;
    std::string submittedPassword;
    std::string submittedPin;
};

} // namespace

// --- A. Startup paths and state gates ---------------------------------------

TEST_F(AuthControllerTest, StartsInRestoringWithAuthenticatingStage)
{
    AuthController controller(authService);

    EXPECT_EQ(controller.authState(), AuthController::Restoring);
    EXPECT_EQ(controller.loadingStage(), AuthController::Authenticating);
}

TEST_F(AuthControllerTest, RestoreSessionWithNoStoredSessionShowsLoginScreen)
{
    // Arrange -- SetUp's default loadSession() reports nothing stored.
    AuthController controller(authService);

    // Act
    controller.restoreSession();
    flush();

    // Assert -- a first launch is not an error, so no message is shown.
    EXPECT_EQ(controller.authState(), AuthController::LoggedOut);
    EXPECT_EQ(controller.authErrorKind(), AuthController::NoError);
    EXPECT_EQ(controller.loadingStage(), AuthController::NotLoading);
}

TEST_F(AuthControllerTest, RestoreSessionSuccessGoesToLoggedIn)
{
    // Arrange
    EXPECT_CALL(*mockSessionStore, loadSession())
        .WillRepeatedly(Return(Result<std::string>::ok("stored-token")));
    EXPECT_CALL(*mockClient, loginWithSession(_, _))
        .WillOnce(InvokeArgument<1>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(_, _)).WillOnce(InvokeArgument<1>(Result<void>::ok()));
    AuthController controller(authService);

    // Act
    controller.restoreSession();
    flush();

    // Assert
    EXPECT_EQ(controller.authState(), AuthController::LoggedIn);
    EXPECT_EQ(controller.loadingStage(), AuthController::NotLoading);
}

TEST_F(AuthControllerTest, RestoreSessionDefinitivelyInvalidShowsNoMessage)
{
    // Arrange -- AuthService has already cleared the stored session, so there
    // is nothing for the user to act on.
    EXPECT_CALL(*mockSessionStore, loadSession())
        .WillRepeatedly(Return(Result<std::string>::ok("stale-token")));
    EXPECT_CALL(*mockClient, loginWithSession(_, _))
        .WillOnce(InvokeArgument<1>(Result<void>::fail("expired", MegaErrorCode::kESid)));
    AuthController controller(authService);

    // Act
    controller.restoreSession();
    flush();

    // Assert
    EXPECT_EQ(controller.authState(), AuthController::LoggedOut);
    EXPECT_EQ(controller.authErrorKind(), AuthController::NoError);
}

TEST_F(AuthControllerTest, RestoreSessionTransientFailureShowsNetworkError)
{
    // Arrange -- the session may well still be good, so this one does get a
    // message.
    EXPECT_CALL(*mockSessionStore, loadSession())
        .WillRepeatedly(Return(Result<std::string>::ok("stored-token")));
    EXPECT_CALL(*mockClient, loginWithSession(_, _))
        .WillOnce(InvokeArgument<1>(Result<void>::fail("try later", MegaErrorCode::kEAgain)));
    AuthController controller(authService);

    // Act
    controller.restoreSession();
    flush();

    // Assert
    EXPECT_EQ(controller.authState(), AuthController::LoggedOut);
    EXPECT_EQ(controller.authErrorKind(), AuthController::NetworkError);
    EXPECT_EQ(controller.loadingStage(), AuthController::NotLoading);
}

TEST_F(AuthControllerTest, SecondRestoreSessionIsIgnored)
{
    // Arrange
    AuthController controller(authService);
    arriveAtLoggedOut(controller);
    EXPECT_CALL(*mockSessionStore, loadSession()).Times(0);

    // Act
    controller.restoreSession();
    flush();

    // Assert -- covered by .Times(0).
    EXPECT_EQ(controller.authState(), AuthController::LoggedOut);
}

TEST_F(AuthControllerTest, LoginIsIgnoredWhileRestoring)
{
    // Arrange -- the login form is not reachable yet, but the gate is what
    // stops a queued restore result from racing a manual login.
    AuthController controller(authService);
    EXPECT_CALL(*mockClient, login(_, _, _)).Times(0);

    // Act
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
    flush();

    // Assert -- covered by .Times(0).
    EXPECT_EQ(controller.authState(), AuthController::Restoring);
}

TEST_F(AuthControllerTest, TwoFactorAndLogoutAreIgnoredWhenLoggedOut)
{
    // Arrange
    AuthController controller(authService);
    arriveAtLoggedOut(controller);
    EXPECT_CALL(*mockClient, multiFactorAuthLogin(_, _, _, _)).Times(0);
    EXPECT_CALL(*mockClient, logout(_)).Times(0);

    // Act
    controller.submitTwoFactorCode(QStringLiteral("123456"));
    controller.cancelTwoFactor();
    controller.logout();
    flush();

    // Assert -- covered by the .Times(0) expectations above.
    EXPECT_EQ(controller.authState(), AuthController::LoggedOut);
}

// --- B. LoadingStage is derived from AuthState -------------------------------
//
// setState() is the single choke point that maps state to stage, so that none
// of the terminal paths can forget to stop the timers. Each of those paths
// gets a case here.

TEST_F(AuthControllerTest, LoginEntersAuthenticatingStage)
{
    // Arrange -- hold the login callback so the attempt stays in flight.
    DoneCallback pending;
    EXPECT_CALL(*mockClient, login(_, _, _)).WillOnce(SaveArg<2>(&pending));
    AuthController controller(authService);
    arriveAtLoggedOut(controller);

    // Act
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
    flush();

    // Assert
    EXPECT_EQ(controller.authState(), AuthController::LoggingIn);
    EXPECT_EQ(controller.loadingStage(), AuthController::Authenticating);
}

TEST_F(AuthControllerTest, WrongPasswordReturnsToNotLoading)
{
    // Arrange
    allowLoginFailure(MegaErrorCode::kENoEnt);
    AuthController controller(authService);
    arriveAtLoggedOut(controller);

    // Act
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("wrong"));
    flush();

    // Assert
    EXPECT_EQ(controller.authState(), AuthController::LoggedOut);
    EXPECT_EQ(controller.loadingStage(), AuthController::NotLoading);
}

TEST_F(AuthControllerTest, MfaRequiredReturnsToNotLoading)
{
    // Arrange
    allowLoginFailure(MegaErrorCode::kEMfaRequired, "2FA required");
    AuthController controller(authService);
    arriveAtLoggedOut(controller);

    // Act
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
    flush();

    // Assert -- the 2FA prompt is not a loading screen.
    EXPECT_EQ(controller.authState(), AuthController::NeedsTwoFactor);
    EXPECT_EQ(controller.loadingStage(), AuthController::NotLoading);
    EXPECT_EQ(controller.authErrorKind(), AuthController::NoError);
}

TEST_F(AuthControllerTest, WrongTwoFactorCodeReturnsToNotLoading)
{
    // Arrange
    allowLoginFailure(MegaErrorCode::kEMfaRequired, "2FA required");
    captureTwoFactorSubmission();
    AuthController controller(authService);
    arriveAtLoggedOut(controller);
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
    flush();
    ASSERT_EQ(controller.authState(), AuthController::NeedsTwoFactor);

    // Act
    controller.submitTwoFactorCode(QStringLiteral("000000"));
    flush();

    // Assert
    EXPECT_EQ(controller.authState(), AuthController::NeedsTwoFactor);
    EXPECT_EQ(controller.loadingStage(), AuthController::NotLoading);
    EXPECT_EQ(controller.authErrorKind(), AuthController::InvalidCredentials);
}

TEST_F(AuthControllerTest, CancelTwoFactorReturnsToNotLoading)
{
    // Arrange
    allowLoginFailure(MegaErrorCode::kEMfaRequired, "2FA required");
    AuthController controller(authService);
    arriveAtLoggedOut(controller);
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
    flush();
    ASSERT_EQ(controller.authState(), AuthController::NeedsTwoFactor);

    // Act
    controller.cancelTwoFactor();

    // Assert
    EXPECT_EQ(controller.authState(), AuthController::LoggedOut);
    EXPECT_EQ(controller.loadingStage(), AuthController::NotLoading);
    EXPECT_EQ(controller.authErrorKind(), AuthController::NoError);
}

TEST_F(AuthControllerTest, LogoutShowsSigningOutThenNotLoading)
{
    // Arrange -- hold the logout callback so SigningOut is observable.
    AuthController controller(authService);
    arriveAtLoggedIn(controller);
    DoneCallback pending;
    EXPECT_CALL(*mockClient, logout(_)).WillOnce(SaveArg<0>(&pending));

    // Act
    controller.logout();
    flush();

    // Assert -- mid-flight.
    EXPECT_EQ(controller.authState(), AuthController::LoggingOut);
    EXPECT_EQ(controller.loadingStage(), AuthController::SigningOut);

    // Act -- the SDK answers.
    pending(Result<void>::ok());
    flush();

    // Assert
    EXPECT_EQ(controller.authState(), AuthController::LoggedOut);
    EXPECT_EQ(controller.loadingStage(), AuthController::NotLoading);
}

// --- C. Fetch progress -------------------------------------------------------

TEST_F(AuthControllerTest, UnknownTotalKeepsProgressZeroAndTextEmpty)
{
    // Arrange -- the response length is not always known on the first events,
    // and there is deliberately no bar without a denominator.
    ProgressCallback progress;
    EXPECT_CALL(*mockClient, login(_, _, _)).WillOnce(InvokeArgument<2>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(_, _)).WillOnce(SaveArg<0>(&progress));
    AuthController controller(authService);
    arriveAtLoggedOut(controller);
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
    flush();
    ASSERT_TRUE(progress);

    // Act
    progress(0, 0);
    flush();

    // Assert -- still on the authenticating message, no bar.
    EXPECT_EQ(controller.loadingStage(), AuthController::Authenticating);
    EXPECT_DOUBLE_EQ(controller.fetchProgress(), 0.0);
    EXPECT_TRUE(controller.fetchProgressText().isEmpty());
}

TEST_F(AuthControllerTest, KnownTotalMovesToDownloadingNodes)
{
    // Arrange
    ProgressCallback progress;
    EXPECT_CALL(*mockClient, login(_, _, _)).WillOnce(InvokeArgument<2>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(_, _)).WillOnce(SaveArg<0>(&progress));
    AuthController controller(authService);
    arriveAtLoggedOut(controller);
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
    flush();
    ASSERT_TRUE(progress);

    // Act
    progress(64, 183);
    flush();

    // Assert
    EXPECT_EQ(controller.loadingStage(), AuthController::DownloadingNodes);
    EXPECT_NEAR(controller.fetchProgress(), 64.0 / 183.0, 0.001);
    // Locale-formatted, so only its shape is asserted.
    EXPECT_FALSE(controller.fetchProgressText().isEmpty());
    EXPECT_TRUE(controller.fetchProgressText().contains(QStringLiteral("/")));
}

TEST_F(AuthControllerTest, ProgressBeyondTotalClampsToOne)
{
    // Arrange -- progress is not monotonic and the request can be retried, so
    // transferred > total is reachable.
    ProgressCallback progress;
    EXPECT_CALL(*mockClient, login(_, _, _)).WillOnce(InvokeArgument<2>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(_, _)).WillOnce(SaveArg<0>(&progress));
    AuthController controller(authService);
    arriveAtLoggedOut(controller);
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
    flush();
    ASSERT_TRUE(progress);

    // Act
    progress(200, 183);
    flush();

    // Assert
    EXPECT_DOUBLE_EQ(controller.fetchProgress(), 1.0);
}

TEST_F(AuthControllerTest, ReachingLoggedInClearsProgress)
{
    // Arrange
    ProgressCallback progress;
    DoneCallback done;
    EXPECT_CALL(*mockClient, login(_, _, _)).WillOnce(InvokeArgument<2>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(_, _))
        .WillOnce(DoAll(SaveArg<0>(&progress), SaveArg<1>(&done)));
    AuthController controller(authService);
    arriveAtLoggedOut(controller);
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
    flush();
    progress(64, 183);
    flush();
    ASSERT_EQ(controller.loadingStage(), AuthController::DownloadingNodes);

    // Act
    done(Result<void>::ok());
    flush();

    // Assert -- a stale bar must not survive into the next loading screen.
    EXPECT_EQ(controller.loadingStage(), AuthController::NotLoading);
    EXPECT_DOUBLE_EQ(controller.fetchProgress(), 0.0);
    EXPECT_TRUE(controller.fetchProgressText().isEmpty());
}

TEST_F(AuthControllerTest, EachProgressEventEmitsLoadingStateChanged)
{
    // Arrange
    ProgressCallback progress;
    EXPECT_CALL(*mockClient, login(_, _, _)).WillOnce(InvokeArgument<2>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(_, _)).WillOnce(SaveArg<0>(&progress));
    AuthController controller(authService);
    arriveAtLoggedOut(controller);
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
    flush();
    int stateSignals = 0;
    QObject::connect(&controller, &AuthController::loadingStateChanged, &controller, [&]() {
        ++stateSignals;
    });

    // Act
    progress(64, 183);
    flush();
    progress(128, 183);
    flush();

    // Assert -- the first event also changes the stage, which emits once more.
    EXPECT_GE(stateSignals, 2);
    EXPECT_NEAR(controller.fetchProgress(), 128.0 / 183.0, 0.001);
}

// --- D. Stall timer ----------------------------------------------------------

TEST_F(AuthControllerTest, StallTimeoutMovesDownloadingToDecrypting)
{
    // Arrange -- the last real event observed was 99.44%, so the handover is
    // detected by the byte progress going quiet, not by reaching 100%.
    ProgressCallback progress;
    EXPECT_CALL(*mockClient, login(_, _, _)).WillOnce(InvokeArgument<2>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(_, _)).WillOnce(SaveArg<0>(&progress));
    AuthController controller(authService, 20);
    arriveAtLoggedOut(controller);
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
    flush();
    progress(182, 183);
    flush();
    ASSERT_EQ(controller.loadingStage(), AuthController::DownloadingNodes);

    // Act / Assert
    EXPECT_TRUE(waitForStage(controller, AuthController::DecryptingNodes));
}

TEST_F(AuthControllerTest, StallIsNotALatch)
{
    // Arrange -- a genuinely stalled connection that recovers must go back to
    // the download message rather than sit on the wrong one.
    ProgressCallback progress;
    EXPECT_CALL(*mockClient, login(_, _, _)).WillOnce(InvokeArgument<2>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(_, _)).WillOnce(SaveArg<0>(&progress));
    AuthController controller(authService, 20);
    arriveAtLoggedOut(controller);
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
    flush();
    progress(64, 183);
    flush();
    ASSERT_TRUE(waitForStage(controller, AuthController::DecryptingNodes));

    // Act -- the connection wakes up again.
    progress(128, 183);
    flush();

    // Assert
    EXPECT_EQ(controller.loadingStage(), AuthController::DownloadingNodes);
    EXPECT_NEAR(controller.fetchProgress(), 128.0 / 183.0, 0.001);
}

TEST_F(AuthControllerTest, LaterProgressRestartsTheStallTimer)
{
    // Arrange -- with a 300ms timeout, an event at ~200ms must push the
    // handover past the 400ms mark; without the restart it would have fired
    // at 300ms.
    ProgressCallback progress;
    EXPECT_CALL(*mockClient, login(_, _, _)).WillOnce(InvokeArgument<2>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(_, _)).WillOnce(SaveArg<0>(&progress));
    AuthController controller(authService, 300);
    arriveAtLoggedOut(controller);
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
    flush();
    progress(64, 183);
    flush();

    // Act
    spinFor(200);
    progress(128, 183);
    flush();
    spinFor(200);

    // Assert
    EXPECT_EQ(controller.loadingStage(), AuthController::DownloadingNodes);
}

TEST_F(AuthControllerTest, StaleStallTimerCannotFlipASignedInWindow)
{
    // Arrange -- a stall timer still armed when the fetch completes must not
    // flip a signed-in window back to "decrypting". Two things prevent that
    // independently: setLoadingStage() stops the timer, and the timeout
    // handler re-checks the stage. This pins the outcome, not either
    // mechanism -- commenting out the stop() alone keeps this green.
    ProgressCallback progress;
    DoneCallback done;
    EXPECT_CALL(*mockClient, login(_, _, _)).WillOnce(InvokeArgument<2>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(_, _))
        .WillOnce(DoAll(SaveArg<0>(&progress), SaveArg<1>(&done)));
    AuthController controller(authService, 20);
    arriveAtLoggedOut(controller);
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
    flush();
    progress(64, 183);
    flush();
    ASSERT_EQ(controller.loadingStage(), AuthController::DownloadingNodes);

    // Act
    done(Result<void>::ok());
    flush();
    spinFor(100);

    // Assert
    EXPECT_EQ(controller.authState(), AuthController::LoggedIn);
    EXPECT_EQ(controller.loadingStage(), AuthController::NotLoading);
}

// --- E. Generation guard on fetch progress -----------------------------------

TEST_F(AuthControllerTest, StaleProgressAfterReloginIsIgnored)
{
    // Arrange -- a fetch abandoned by a logout can still have queued progress
    // events in flight; they must not paint the next session's screen.
    ProgressCallback firstProgress;
    ProgressCallback secondProgress;
    DoneCallback firstDone;
    EXPECT_CALL(*mockClient, login(_, _, _)).WillRepeatedly(InvokeArgument<2>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(_, _))
        .WillOnce(DoAll(SaveArg<0>(&firstProgress), SaveArg<1>(&firstDone)))
        .WillOnce(SaveArg<0>(&secondProgress));
    AuthController controller(authService);
    arriveAtLoggedOut(controller);

    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
    flush();
    firstDone(Result<void>::ok());
    flush();
    ASSERT_EQ(controller.authState(), AuthController::LoggedIn);
    controller.logout();
    flush();
    controller.login(QStringLiteral("grace@example.com"), QStringLiteral("pw2"));
    flush();
    ASSERT_TRUE(secondProgress);

    // Act -- the abandoned fetch finally reports.
    firstProgress(64, 183);
    flush();

    // Assert
    EXPECT_EQ(controller.loadingStage(), AuthController::Authenticating);
    EXPECT_DOUBLE_EQ(controller.fetchProgress(), 0.0);
    EXPECT_TRUE(controller.fetchProgressText().isEmpty());
}

TEST_F(AuthControllerTest, CurrentGenerationProgressIsStillHonoured)
{
    // Arrange -- the other half of the test above: without this, that one
    // would also pass if progress were never delivered at all.
    ProgressCallback firstProgress;
    ProgressCallback secondProgress;
    DoneCallback firstDone;
    EXPECT_CALL(*mockClient, login(_, _, _)).WillRepeatedly(InvokeArgument<2>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, fetchNodes(_, _))
        .WillOnce(DoAll(SaveArg<0>(&firstProgress), SaveArg<1>(&firstDone)))
        .WillOnce(SaveArg<0>(&secondProgress));
    AuthController controller(authService);
    arriveAtLoggedOut(controller);

    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
    flush();
    firstDone(Result<void>::ok());
    flush();
    controller.logout();
    flush();
    controller.login(QStringLiteral("grace@example.com"), QStringLiteral("pw2"));
    flush();
    ASSERT_TRUE(secondProgress);

    // Act
    secondProgress(64, 183);
    flush();

    // Assert
    EXPECT_EQ(controller.loadingStage(), AuthController::DownloadingNodes);
    EXPECT_NEAR(controller.fetchProgress(), 64.0 / 183.0, 0.001);
}

TEST_F(AuthControllerTest, StaleProgressFromAbandonedRestoreIsIgnored)
{
    // Arrange -- a restore that failed transiently mid-fetch, followed by the
    // user signing in by hand. Same guard, different entry point.
    EXPECT_CALL(*mockSessionStore, loadSession())
        .WillRepeatedly(Return(Result<std::string>::ok("stored-token")));
    EXPECT_CALL(*mockClient, loginWithSession(_, _))
        .WillOnce(InvokeArgument<1>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, login(_, _, _)).WillOnce(InvokeArgument<2>(Result<void>::ok()));
    ProgressCallback restoreProgress;
    ProgressCallback loginProgress;
    DoneCallback restoreDone;
    EXPECT_CALL(*mockClient, fetchNodes(_, _))
        .WillOnce(DoAll(SaveArg<0>(&restoreProgress), SaveArg<1>(&restoreDone)))
        .WillOnce(SaveArg<0>(&loginProgress));
    AuthController controller(authService);

    controller.restoreSession();
    flush();
    restoreDone(Result<void>::fail("try later", MegaErrorCode::kEAgain));
    flush();
    ASSERT_EQ(controller.authState(), AuthController::LoggedOut);
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
    flush();
    ASSERT_TRUE(loginProgress);

    // Act
    restoreProgress(64, 183);
    flush();

    // Assert
    EXPECT_EQ(controller.loadingStage(), AuthController::Authenticating);
    EXPECT_DOUBLE_EQ(controller.fetchProgress(), 0.0);
}

// --- F. Pending 2FA credentials ---------------------------------------------
//
// mPendingEmail/mPendingPassword are private, so what is verified here is the
// behaviour that matters: a 2FA submission never carries credentials from a
// previous attempt. Whether the plaintext is actually scrubbed from process
// memory is not observable from a test (see R4-2 in docs/REFACTOR_PLANS.md).

TEST_F(AuthControllerTest, TwoFactorSubmitsTheCredentialsFromTheFailedLogin)
{
    // Arrange
    allowLoginFailure(MegaErrorCode::kEMfaRequired, "2FA required");
    captureTwoFactorSubmission();
    AuthController controller(authService);
    arriveAtLoggedOut(controller);
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw1"));
    flush();
    ASSERT_EQ(controller.authState(), AuthController::NeedsTwoFactor);

    // Act
    controller.submitTwoFactorCode(QStringLiteral("123456"));
    flush();

    // Assert
    EXPECT_EQ(submittedEmail, "ada@example.com");
    EXPECT_EQ(submittedPassword, "pw1");
    EXPECT_EQ(submittedPin, "123456");
}

TEST_F(AuthControllerTest, TwoFactorFailureKeepsCredentialsForRetry)
{
    // Arrange -- a mistyped PIN must not force the whole login again.
    allowLoginFailure(MegaErrorCode::kEMfaRequired, "2FA required");
    captureTwoFactorSubmission();
    AuthController controller(authService);
    arriveAtLoggedOut(controller);
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw1"));
    flush();
    controller.submitTwoFactorCode(QStringLiteral("000000"));
    flush();
    ASSERT_EQ(controller.authState(), AuthController::NeedsTwoFactor);

    // Act
    controller.submitTwoFactorCode(QStringLiteral("123456"));
    flush();

    // Assert
    EXPECT_EQ(submittedEmail, "ada@example.com");
    EXPECT_EQ(submittedPassword, "pw1");
    EXPECT_EQ(submittedPin, "123456");
}

TEST_F(AuthControllerTest, CancelledTwoFactorCredentialsAreNotReused)
{
    // Arrange
    allowLoginFailure(MegaErrorCode::kEMfaRequired, "2FA required");
    captureTwoFactorSubmission();
    AuthController controller(authService);
    arriveAtLoggedOut(controller);
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw1"));
    flush();
    controller.cancelTwoFactor();
    ASSERT_EQ(controller.authState(), AuthController::LoggedOut);

    // Act -- a different account this time.
    controller.login(QStringLiteral("grace@example.com"), QStringLiteral("pw2"));
    flush();
    controller.submitTwoFactorCode(QStringLiteral("654321"));
    flush();

    // Assert
    EXPECT_EQ(submittedEmail, "grace@example.com");
    EXPECT_EQ(submittedPassword, "pw2");
}

TEST_F(AuthControllerTest, CredentialsFromASuccessfulTwoFactorAreNotReused)
{
    // Arrange -- sign in with 2FA, sign out, then sign in as someone else.
    EXPECT_CALL(*mockClient, login(_, _, _))
        .WillRepeatedly(
            InvokeArgument<2>(Result<void>::fail("2FA required", MegaErrorCode::kEMfaRequired)));
    EXPECT_CALL(*mockClient, fetchNodes(_, _))
        .WillRepeatedly(InvokeArgument<1>(Result<void>::ok()));
    EXPECT_CALL(*mockClient, multiFactorAuthLogin(_, _, _, _))
        .WillOnce(DoAll(SaveArg<0>(&submittedEmail),
                        SaveArg<1>(&submittedPassword),
                        InvokeArgument<3>(Result<void>::ok())))
        .WillOnce(DoAll(SaveArg<0>(&submittedEmail),
                        SaveArg<1>(&submittedPassword),
                        InvokeArgument<3>(Result<void>::ok())));
    AuthController controller(authService);
    arriveAtLoggedOut(controller);
    controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw1"));
    flush();
    controller.submitTwoFactorCode(QStringLiteral("123456"));
    flush();
    ASSERT_EQ(controller.authState(), AuthController::LoggedIn);
    controller.logout();
    flush();

    // Act
    controller.login(QStringLiteral("grace@example.com"), QStringLiteral("pw2"));
    flush();
    controller.submitTwoFactorCode(QStringLiteral("654321"));
    flush();

    // Assert
    EXPECT_EQ(submittedEmail, "grace@example.com");
    EXPECT_EQ(submittedPassword, "pw2");
}

// --- G. Error classification -------------------------------------------------

TEST_F(AuthControllerTest, LoginErrorsMapToErrorKinds)
{
    struct Case
    {
        int errorCode;
        AuthController::AuthErrorKind expected;
    };
    // kEInternal is what an unclassified SDK failure carries, so it lands in
    // UnknownError and the raw English sentence reaches the login screen.
    // Pinned as-is here; whether that is the right UX is carried over in
    // docs/REFACTOR_PLANS.md section 5.
    const std::vector<Case> cases = {
        {MegaErrorCode::kENoEnt, AuthController::InvalidCredentials},
        {MegaErrorCode::kEBlocked, AuthController::AccountBlocked},
        {MegaErrorCode::kETooMany, AuthController::TooManyAttempts},
        {MegaErrorCode::kEAgain, AuthController::NetworkError},
        {MegaErrorCode::kEAccess, AuthController::UnknownError},
        {MegaErrorCode::kEInternal, AuthController::UnknownError},
    };

    for (const Case& testCase : cases)
    {
        SCOPED_TRACE(testCase.errorCode);

        // Arrange
        allowLoginFailure(testCase.errorCode, "Server said no");
        AuthController controller(authService);
        arriveAtLoggedOut(controller);

        // Act
        controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
        flush();

        // Assert -- the raw text is passed through only when QML has no
        // sentence of its own to show.
        EXPECT_EQ(controller.authErrorKind(), testCase.expected);
        EXPECT_EQ(controller.authState(), AuthController::LoggedOut);
        if (testCase.expected == AuthController::UnknownError)
            EXPECT_EQ(controller.rawErrorMessage(), QStringLiteral("Server said no"));
        else
            EXPECT_TRUE(controller.rawErrorMessage().isEmpty());
    }
}

TEST_F(AuthControllerTest, TwoFactorErrorsMapToErrorKinds)
{
    struct Case
    {
        int errorCode;
        AuthController::AuthErrorKind expected;
    };
    // megaapi.h documents no distinct "wrong PIN" code, so the three plausible
    // candidates are folded into InvalidCredentials -- which is where this
    // differs from the login mapping above.
    const std::vector<Case> cases = {
        {MegaErrorCode::kENoEnt, AuthController::InvalidCredentials},
        {MegaErrorCode::kEFailed, AuthController::InvalidCredentials},
        {MegaErrorCode::kEExpired, AuthController::InvalidCredentials},
        {MegaErrorCode::kEBlocked, AuthController::AccountBlocked},
        {MegaErrorCode::kEInternal, AuthController::UnknownError},
    };

    for (const Case& testCase : cases)
    {
        SCOPED_TRACE(testCase.errorCode);

        // Arrange
        allowLoginFailure(MegaErrorCode::kEMfaRequired, "2FA required");
        captureTwoFactorSubmission(testCase.errorCode);
        AuthController controller(authService);
        arriveAtLoggedOut(controller);
        controller.login(QStringLiteral("ada@example.com"), QStringLiteral("pw"));
        flush();
        ASSERT_EQ(controller.authState(), AuthController::NeedsTwoFactor);

        // Act
        controller.submitTwoFactorCode(QStringLiteral("000000"));
        flush();

        // Assert -- the user stays on the PIN prompt either way.
        EXPECT_EQ(controller.authErrorKind(), testCase.expected);
        EXPECT_EQ(controller.authState(), AuthController::NeedsTwoFactor);
        if (testCase.expected == AuthController::UnknownError)
            EXPECT_EQ(controller.rawErrorMessage(), QStringLiteral("rejected"));
        else
            EXPECT_TRUE(controller.rawErrorMessage().isEmpty());
    }
}
