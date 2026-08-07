#pragma once
#include "core/AuthService.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <memory>
#include <QtQml/qqmlregistration.h>
#include <string>

// QML-facing GUI glue wrapping AuthService. Registered as a QML type purely so QML
// can name AuthController.LoggedIn and friends; the composition root still hands
// QML the single instance as a context property, QML never constructs one.
//
// Deliberately has no NotificationController dependency, unlike every other
// controller here: login/2FA failures are shown inline on the login screen, a
// restore failure is silent or inline on that same screen, and logout always
// succeeds from the caller's perspective.

// How long the fetchNodes byte progress must stay quiet before the loading screen
// calls the download done and switches to the decrypt message. Measured ~1.15s
// between events, so 8s is far outside normal jitter while short enough that the
// bar doesn't sit visibly frozen. The SDK cannot emit further updates once the
// response is complete, so a long quiet spell really does mean it finished.
//
// Overridable through the constructor only so tests need not spend 8 real seconds
// per case.
constexpr int kDefaultStallTimeoutMs = 8000;

class AuthController : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided as the authController context property")

public:
    enum AuthState
    {
        Restoring,
        LoggedOut,
        LoggingIn,
        NeedsTwoFactor,
        VerifyingTwoFactor,
        LoggedIn,
        LoggingOut
    };
    Q_ENUM(AuthState)

    enum AuthErrorKind
    {
        NoError,
        InvalidCredentials,
        AccountBlocked,
        TooManyAttempts,
        NetworkError,
        UnknownError
    };
    Q_ENUM(AuthErrorKind)

    // What the login screen is waiting on. AuthState alone can't say: LoggingIn
    // covers both the authentication round-trip and the node fetch after it, which
    // was measured at 6m25s of blank screen on a large account.
    //
    // Only DownloadingNodes has a real percentage, and it is ~42% of the wait -- the
    // decrypt/tree-build phase has no progress signal in the SDK at all. Hence
    // separate stages rather than one blended bar: a "downloading" bar reaching 100%
    // and handing over is honest, a "loading" bar frozen at 100% for three more
    // minutes is not.
    //
    // Deliberately nothing between Authenticating and DownloadingNodes: the
    // account-info round-trip there is 1.1s and the wait for the first byte 3.0s,
    // not worth their own wording.
    enum LoadingStage
    {
        NotLoading,
        Authenticating,
        DownloadingNodes,
        DecryptingNodes,
        SigningOut
    };
    Q_ENUM(LoadingStage)

    Q_PROPERTY(AuthState authState READ authState NOTIFY authStateChanged)
    Q_PROPERTY(AuthErrorKind authErrorKind READ authErrorKind NOTIFY authErrorKindChanged)

    // One signal for the whole loading cluster.
    Q_PROPERTY(LoadingStage loadingStage READ loadingStage NOTIFY loadingStateChanged)
    Q_PROPERTY(qreal fetchProgress READ fetchProgress NOTIFY loadingStateChanged)
    Q_PROPERTY(QString fetchProgressText READ fetchProgressText NOTIFY loadingStateChanged)

    explicit AuthController(std::shared_ptr<AuthService> authService,
                            int stallTimeoutMs = kDefaultStallTimeoutMs,
                            QObject* parent = nullptr);

    // Not Q_INVOKABLE: called once from the composition root before app.exec().
    void restoreSession();

    Q_INVOKABLE void login(const QString& email, const QString& password);
    Q_INVOKABLE void submitTwoFactorCode(const QString& pin);
    Q_INVOKABLE void cancelTwoFactor();
    Q_INVOKABLE void logout();

    AuthState authState() const;
    AuthErrorKind authErrorKind() const;

    LoadingStage loadingStage() const;
    qreal fetchProgress() const; // 0.0-1.0, 0.0 while the total is unknown
    // Locale-formatted "62.0 MB / 183.3 MB", empty until a total is known. Done here
    // because QML has no formattedDataSize equivalent.
    QString fetchProgressText() const;

signals:
    void authStateChanged();
    void authErrorKindChanged();
    void loadingStateChanged();

private:
    void setState(AuthState state);
    // Takes the kind only: every kind maps to a fixed localized sentence in QML, and
    // the SDK's own English never leaves the qCWarning at the call site.
    void setError(AuthErrorKind kind);
    AuthErrorKind classifyError(int errorCode) const;

    void setLoadingStage(LoadingStage stage);
    // The returned lambda hops to the GUI thread and ignores events from an earlier
    // attempt (mLoadGeneration).
    AuthService::FetchProgressCallback makeFetchProgressCallback();
    void handleFetchProgress(std::uint64_t transferredBytes, std::uint64_t totalBytes);

    std::shared_ptr<AuthService> mAuthService;
    AuthState mState = Restoring;
    AuthErrorKind mErrorKind = NoError;

    LoadingStage mLoadingStage = Authenticating; // matches mState's Restoring
    std::uint64_t mTransferredBytes = 0;
    std::uint64_t mTotalBytes = 0;
    // Bumped per login attempt: a fetch abandoned by a logout/re-login can still have
    // queued progress events in flight, which carry the old generation.
    std::uint64_t mLoadGeneration = 0;
    // How the download -> decrypt handover is detected: the last event observed in
    // practice was 99.44%, so waiting for an exact 100% would hang the bar. Not a
    // one-way latch -- a later event moves the stage back, so a genuinely stalled
    // connection recovers instead of showing the wrong message forever.
    QTimer mStallTimer;
    // Held between login()'s kEMfaRequired failure and the matching
    // submitTwoFactorCode()/cancelTwoFactor(); cleared on either path.
    std::string mPendingEmail;
    std::string mPendingPassword;
};
