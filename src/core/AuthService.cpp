#include "AuthService.h"

#include "MegaErrorCodes.h"

bool isSessionDefinitivelyInvalid(int errorCode)
{
    switch (errorCode)
    {
        case MegaErrorCode::kEArgs:
        case MegaErrorCode::kEExpired:
        case MegaErrorCode::kENoEnt:
        case MegaErrorCode::kEAccess:
        case MegaErrorCode::kESid:
        case MegaErrorCode::kEBlocked:
            return true;
        default:
            return false;
    }
}

AuthService::AuthService(std::shared_ptr<IMegaClient> client,
                         std::shared_ptr<ISessionStore> sessionStore)
    : mClient(std::move(client)), mSessionStore(std::move(sessionStore))
{}

void AuthService::restoreSession(FetchProgressCallback onFetchProgress,
                                 std::function<void(Result<void>)> onDone)
{
    Result<std::string> stored = mSessionStore->loadSession();
    if (!stored.success)
    {
        // Corrupt/undecryptable session file -- self-heal so it doesn't
        // keep failing on every future launch.
        (void)mSessionStore->clearSession();
        onDone(Result<void>::fail(stored.errorMessage, MegaErrorCode::kESid));
        return;
    }

    if (stored.value.empty())
    {
        onDone(Result<void>::fail("no stored session", kNoStoredSession));
        return;
    }

    mClient->loginWithSession(stored.value, [this, onFetchProgress, onDone](Result<void> result) {
        if (!result.success)
        {
            if (isSessionDefinitivelyInvalid(result.errorCode))
                (void)mSessionStore->clearSession();
            onDone(std::move(result));
            return;
        }
        finishLoginSuccess(onFetchProgress, onDone);
    });
}

void AuthService::login(const std::string& email,
                        const std::string& password,
                        FetchProgressCallback onFetchProgress,
                        std::function<void(Result<void>)> onDone)
{
    mClient->login(email, password, [this, onFetchProgress, onDone](Result<void> result) {
        if (!result.success)
        {
            onDone(std::move(result));
            return;
        }
        finishLoginSuccess(onFetchProgress, onDone);
    });
}

void AuthService::loginWithTwoFactor(const std::string& email,
                                     const std::string& password,
                                     const std::string& pin,
                                     FetchProgressCallback onFetchProgress,
                                     std::function<void(Result<void>)> onDone)
{
    mClient->multiFactorAuthLogin(
        email, password, pin, [this, onFetchProgress, onDone](Result<void> result) {
            if (!result.success)
            {
                onDone(std::move(result));
                return;
            }
            finishLoginSuccess(onFetchProgress, onDone);
        });
}

void AuthService::logout(std::function<void(Result<void>)> onDone)
{
    mClient->logout([this, onDone](Result<void> /*result*/) {
        // Result deliberately ignored -- see the header comment: logout
        // always succeeds from the caller's perspective.
        (void)mSessionStore->clearSession();
        onDone(Result<void>::ok());
    });
}

void AuthService::finishLoginSuccess(FetchProgressCallback onFetchProgress,
                                     std::function<void(Result<void>)> onDone)
{
    mClient->fetchNodes(onFetchProgress, [this, onDone](Result<void> result) {
        if (!result.success)
        {
            if (isSessionDefinitivelyInvalid(result.errorCode))
                (void)mSessionStore->clearSession();
            onDone(std::move(result));
            return;
        }

        Result<std::string> token = mClient->currentSessionToken();
        if (token.success)
        {
            // Best-effort: failure to persist doesn't fail the login itself
            // -- the session is still usable for the rest of this run.
            (void)mSessionStore->saveSession(token.value);
        }
        onDone(Result<void>::ok());
    });
}
