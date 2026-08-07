#include "AuthController.h"

#include "app/Logging.h"
#include "core/MegaErrorCodes.h"
#include "GuiThread.h"

#include <QLocale>
#include <QString>

AuthController::AuthController(std::shared_ptr<AuthService> authService,
                               int stallTimeoutMs,
                               QObject* parent)
    : QObject(parent), mAuthService(std::move(authService))
{
    mStallTimer.setSingleShot(true);
    mStallTimer.setInterval(stallTimeoutMs);
    connect(&mStallTimer, &QTimer::timeout, this, [this]() {
        if (mLoadingStage == DownloadingNodes)
            setLoadingStage(DecryptingNodes);
    });
}

AuthService::FetchProgressCallback AuthController::makeFetchProgressCallback()
{
    const std::uint64_t generation = ++mLoadGeneration;
    return [this, generation](std::uint64_t transferredBytes, std::uint64_t totalBytes) {
        invokeOnGuiThread(this, [this, generation, transferredBytes, totalBytes]() {
            if (generation != mLoadGeneration)
                return; // superseded by a newer login attempt
            handleFetchProgress(transferredBytes, totalBytes);
        });
    };
}

void AuthController::handleFetchProgress(std::uint64_t transferredBytes, std::uint64_t totalBytes)
{
    // The response length isn't always known on the first events, and the
    // request can be retried, so progress is not monotonic and totalBytes can
    // be 0. Neither is worth a special stage -- just don't show a bar until
    // there's a denominator.
    if (totalBytes == 0)
        return;

    mTransferredBytes = transferredBytes;
    mTotalBytes = totalBytes;
    setLoadingStage(DownloadingNodes);
    mStallTimer.start(); // after setLoadingStage, which stops it
    emit loadingStateChanged();
}

void AuthController::setLoadingStage(LoadingStage stage)
{
    if (mLoadingStage == stage)
        return;
    mLoadingStage = stage;

    mStallTimer.stop();
    if (stage == NotLoading || stage == Authenticating)
    {
        mTransferredBytes = 0;
        mTotalBytes = 0;
    }
    emit loadingStateChanged();
}

void AuthController::restoreSession()
{
    if (mState != Restoring)
        return;

    mAuthService->restoreSession(makeFetchProgressCallback(), [this](Result<void> result) {
        invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
            if (result.success)
            {
                setState(LoggedIn);
                return;
            }
            if (result.errorCode == kNoStoredSession ||
                isSessionDefinitivelyInvalid(result.errorCode))
            {
                // Not an error -- either nothing was ever saved, or the
                // stored session was actively (and definitively) rejected
                // and AuthService already cleared it. Either way, just show
                // the login screen with no message.
                setError(NoError);
                setState(LoggedOut);
                return;
            }
            qCWarning(lcAuth) << "session restore failed transiently:"
                              << QString::fromStdString(result.errorMessage)
                              << "code=" << result.errorCode;
            setError(NetworkError);
            setState(LoggedOut);
        });
    });
}

void AuthController::login(const QString& email, const QString& password)
{
    if (mState != LoggedOut)
        return;

    setError(NoError);
    setState(LoggingIn);

    const std::string emailStd = email.toStdString();
    const std::string passwordStd = password.toStdString();

    mAuthService->login(
        emailStd,
        passwordStd,
        makeFetchProgressCallback(),
        [this, emailStd, passwordStd](Result<void> result) {
            invokeOnGuiThread(
                this, [this, result = std::move(result), emailStd, passwordStd]() mutable {
                    if (result.success)
                    {
                        mPendingEmail.clear();
                        mPendingPassword.clear();
                        setState(LoggedIn);
                        return;
                    }
                    if (result.errorCode == MegaErrorCode::kEMfaRequired)
                    {
                        mPendingEmail = emailStd;
                        mPendingPassword = passwordStd;
                        setState(NeedsTwoFactor);
                        return;
                    }
                    qCWarning(lcAuth)
                        << "login failed:" << QString::fromStdString(result.errorMessage)
                        << "code=" << result.errorCode;
                    setError(classifyError(result.errorCode));
                    setState(LoggedOut);
                });
        });
}

void AuthController::submitTwoFactorCode(const QString& pin)
{
    if (mState != NeedsTwoFactor)
        return;

    setError(NoError);
    setState(VerifyingTwoFactor);

    mAuthService->loginWithTwoFactor(
        mPendingEmail,
        mPendingPassword,
        pin.toStdString(),
        makeFetchProgressCallback(),
        [this](Result<void> result) {
            invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
                if (result.success)
                {
                    mPendingEmail.clear();
                    mPendingPassword.clear();
                    setState(LoggedIn);
                    return;
                }
                qCWarning(lcAuth) << "2FA verification failed:"
                                  << QString::fromStdString(result.errorMessage)
                                  << "code=" << result.errorCode;
                // megaapi.h doesn't document a distinct error code for "wrong
                // PIN" -- fold the plausible candidates into
                // InvalidCredentials here. Confirm/refine against a real 2FA
                // account during manual smoke testing (see the plan's
                // verification steps / docs/PROGRESS.md's Phase 7 entry).
                switch (result.errorCode)
                {
                    case MegaErrorCode::kENoEnt:
                    case MegaErrorCode::kEFailed:
                    case MegaErrorCode::kEExpired:
                        setError(InvalidCredentials);
                        break;
                    default:
                        setError(classifyError(result.errorCode));
                        break;
                }
                setState(NeedsTwoFactor);
            });
        });
}

void AuthController::cancelTwoFactor()
{
    if (mState != NeedsTwoFactor)
        return;

    mPendingEmail.clear();
    mPendingPassword.clear();
    setError(NoError);
    setState(LoggedOut);
}

void AuthController::logout()
{
    if (mState != LoggedIn)
        return;

    setState(LoggingOut);
    mAuthService->logout([this](Result<void> result) {
        invokeOnGuiThread(this, [this, result = std::move(result)]() mutable {
            (void)result; // AuthService::logout always succeeds -- see its own header comment.
            setError(NoError);
            setState(LoggedOut);
        });
    });
}

AuthController::AuthState AuthController::authState() const
{
    return mState;
}

AuthController::AuthErrorKind AuthController::authErrorKind() const
{
    return mErrorKind;
}

void AuthController::setState(AuthState state)
{
    if (mState == state)
        return;
    mState = state;

    // The single choke point for loading state. Deriving it from AuthState
    // here rather than setting it at each call site means no terminal path
    // can forget to stop the timers -- and there are five of them (wrong
    // password, kEMfaRequired, wrong 2FA code, cancelTwoFactor, a transient
    // restore failure) that never reach fetchNodes at all.
    switch (state)
    {
        case Restoring:
        case LoggingIn:
        case VerifyingTwoFactor:
            setLoadingStage(Authenticating);
            break;
        case LoggingOut:
            setLoadingStage(SigningOut);
            break;
        default:
            setLoadingStage(NotLoading);
            break;
    }

    emit authStateChanged();
}

AuthController::LoadingStage AuthController::loadingStage() const
{
    return mLoadingStage;
}

qreal AuthController::fetchProgress() const
{
    if (mTotalBytes == 0)
        return 0.0;
    const qreal ratio = static_cast<qreal>(mTransferredBytes) / static_cast<qreal>(mTotalBytes);
    return ratio > 1.0 ? 1.0 : ratio;
}

QString AuthController::fetchProgressText() const
{
    if (mTotalBytes == 0)
        return QString();
    const QLocale locale = QLocale::system();
    return QStringLiteral("%1 / %2").arg(
        locale.formattedDataSize(
            static_cast<qint64>(mTransferredBytes), 1, QLocale::DataSizeTraditionalFormat),
        locale.formattedDataSize(
            static_cast<qint64>(mTotalBytes), 1, QLocale::DataSizeTraditionalFormat));
}

void AuthController::setError(AuthErrorKind kind)
{
    mErrorKind = kind;
    emit authErrorKindChanged();
}

AuthController::AuthErrorKind AuthController::classifyError(int errorCode) const
{
    switch (errorCode)
    {
        case MegaErrorCode::kENoEnt:
            return InvalidCredentials;
        case MegaErrorCode::kEBlocked:
            return AccountBlocked;
        case MegaErrorCode::kETooMany:
            return TooManyAttempts;
        case MegaErrorCode::kEAgain:
            return NetworkError;
        default:
            return UnknownError;
    }
}
