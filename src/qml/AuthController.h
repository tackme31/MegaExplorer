#pragma once
#include "core/AuthService.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>
#include <memory>
#include <QtQml/qqmlregistration.h>
#include <string>

// QML-facing GUI glue wrapping AuthService, registered as the "authController"
// context property (main.cpp). Also registered as a QML type
// (QML_ELEMENT/QML_UNCREATABLE) -- this codebase's first -- purely so QML can
// reference AuthController.LoggedIn etc. by type name; main.cpp still hands
// QML the single instance via setContextProperty, QML never constructs one
// itself.
//
// Deliberately has no NotificationController* dependency, unlike every other
// controller in this directory: login/2FA failures are shown inline on the
// login screen (via authErrorKind/rawErrorMessage below), a restore failure
// is either silent or shown inline on that same screen, and logout always
// succeeds from the caller's perspective (AuthService::logout's own
// contract) -- there is no failure path left that needs a global toast.

// How long the fetchNodes byte progress must stay quiet before the loading
// screen calls the download done and switches to the decrypt message.
// Measured: 141 events over 162s, so ~1.15s apart on average -- 8s is far
// outside normal jitter, while being short enough that the bar doesn't sit
// visibly frozen at 99% while we wait. The SDK cannot emit further updates
// once the response is complete (request_response_progress early-returns
// unless the fetchnodes CS request is still pending), so a long quiet spell
// really does mean the download finished.
//
// Overridable via the constructor only so tests need not spend 8 real seconds
// per case waiting for the handover; production always uses this value.
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

    // What the login screen is waiting on right now. AuthState alone can't
    // say: LoggingIn covers both the authentication round-trip and the node
    // fetch that follows it, and on a large account that fetch was measured
    // at 6m25s of blank screen (docs/FETCHNODES_PROGRESS_INVESTIGATION.md).
    //
    // Only DownloadingNodes has a real percentage, and it accounts for just
    // ~42% of the wait -- the decrypt/tree-build phase after it has no
    // progress signal in the SDK at all. Hence separate stages rather than
    // one blended bar: a bar labelled "downloading" reaching 100% and
    // handing over to a different message is honest, a bar labelled
    // "loading" freezing at 100% for another three minutes is not.
    //
    // There is deliberately no stage between Authenticating and
    // DownloadingNodes: the account-info round-trip that sits there is 1.1s
    // and the wait for the first byte 3.0s, which the investigation itself
    // judged not worth its own wording -- and the only way to detect the
    // boundary was an in-band marker the SDK can emit for real.
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
    // Only ever populated for UnknownError -- every other AuthErrorKind maps
    // to a fixed, localized sentence composed entirely in QML (see
    // LoginView.qml's describeError()), same "C++ passes structured fields,
    // QML composes text" convention as NotificationController/ToastStack.qml.
    Q_PROPERTY(QString rawErrorMessage READ rawErrorMessage NOTIFY authErrorKindChanged)

    // One signal for the whole loading cluster, same grouping as
    // authErrorKind/rawErrorMessage above and DownloadController's
    // downloadActiveChanged.
    Q_PROPERTY(LoadingStage loadingStage READ loadingStage NOTIFY loadingStateChanged)
    Q_PROPERTY(qreal fetchProgress READ fetchProgress NOTIFY loadingStateChanged)
    Q_PROPERTY(QString fetchProgressText READ fetchProgressText NOTIFY loadingStateChanged)

    explicit AuthController(std::shared_ptr<AuthService> authService,
                            int stallTimeoutMs = kDefaultStallTimeoutMs,
                            QObject* parent = nullptr);

    // Not Q_INVOKABLE: called once from main.cpp's composition root before
    // app.exec(), same convention as FolderNavigationController::loadRoot.
    void restoreSession();

    Q_INVOKABLE void login(const QString& email, const QString& password);
    Q_INVOKABLE void submitTwoFactorCode(const QString& pin);
    Q_INVOKABLE void cancelTwoFactor();
    Q_INVOKABLE void logout();

    AuthState authState() const;
    AuthErrorKind authErrorKind() const;
    QString rawErrorMessage() const;

    LoadingStage loadingStage() const;
    qreal fetchProgress() const; // 0.0-1.0, 0.0 while the total is unknown
    // Locale-formatted "62.0 MB / 183.3 MB", empty until a total is known.
    // Formatted here rather than in QML for the same reason as
    // FileListModel's size column: QML has no formattedDataSize equivalent.
    QString fetchProgressText() const;

signals:
    void authStateChanged();
    void authErrorKindChanged();
    void loadingStateChanged();

private:
    void setState(AuthState state);
    void setError(AuthErrorKind kind, const QString& rawMessage = QString());
    AuthErrorKind classifyError(int errorCode) const;

    void setLoadingStage(LoadingStage stage);
    // Builds the callback handed to AuthService for one login attempt. The
    // returned lambda hops to the GUI thread and ignores events from an
    // earlier attempt (see mLoadGeneration).
    AuthService::FetchProgressCallback makeFetchProgressCallback();
    void handleFetchProgress(std::uint64_t transferredBytes, std::uint64_t totalBytes);

    std::shared_ptr<AuthService> mAuthService;
    AuthState mState = Restoring;
    AuthErrorKind mErrorKind = NoError;
    QString mRawErrorMessage;

    LoadingStage mLoadingStage = Authenticating; // matches mState's Restoring
    std::uint64_t mTransferredBytes = 0;
    std::uint64_t mTotalBytes = 0;
    // Bumped at the start of every login attempt. A fetch abandoned by a
    // logout/re-login can still have queued progress events in flight; they
    // carry the old generation and are dropped.
    std::uint64_t mLoadGeneration = 0;
    // Fires when the byte progress has gone quiet, which is how the download
    // -> decrypt handover is detected (the last event observed in practice
    // was 99.44%, so waiting for an exact 100% would hang the bar). Not a
    // one-way latch: a later event moves the stage back to DownloadingNodes,
    // so a genuinely stalled connection recovers instead of being stuck
    // showing the wrong message.
    QTimer mStallTimer;
    // Held between login()'s kEMfaRequired failure and the matching
    // submitTwoFactorCode()/cancelTwoFactor() -- cleared on either path.
    std::string mPendingEmail;
    std::string mPendingPassword;
};
