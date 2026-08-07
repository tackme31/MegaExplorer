#pragma once
#include "IMegaClient.h"
#include "ISessionStore.h"
#include "Result.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// restoreSession's "nothing stored" code. Positive, so it can never collide with a
// real SDK error -- those are all <= 0 (MegaErrorCodes.h).
constexpr int kNoStoredSession = 1;

// Whether errorCode means a stored session is definitively unusable, and so safe
// to clearSession(). Unknown codes count as transient: keeping a dead session costs
// one spurious restore failure at next launch, while discarding a good one forces
// the user to re-enter their password. Shared with AuthController so both layers
// classify the same way.
bool isSessionDefinitivelyInvalid(int errorCode);

// Coordinates login/logout and session-token persistence.
//
// Deliberately does not depend on FolderNavigationService: resetting navigation
// state on logout is the caller's job, which keeps the two independently testable
// and avoids two callers racing to reset the same state.
class AuthService
{
public:
    // Passed straight through from IMegaClient::fetchNodes -- read its caveats
    // there; notably an event may carry totalBytes == 0.
    //
    // Required, not optional: the SDK adapter calls it unconditionally, so an empty
    // std::function crashes six minutes into a login. Callers pass an explicit no-op.
    using FetchProgressCallback =
        std::function<void(std::uint64_t transferredBytes, std::uint64_t totalBytes)>;

    AuthService(std::shared_ptr<IMegaClient> client, std::shared_ptr<ISessionStore> sessionStore);

    // onDone's Result:
    //   - ok                              logged in and nodes fetched
    //   - kNoStoredSession                nothing was ever saved; show the login
    //                                     screen silently, this is not an error
    //   - isSessionDefinitivelyInvalid    actively rejected, and already cleared
    //   - anything else                   transient (offline); the stored session
    //                                     was left alone, so report "couldn't
    //                                     connect" rather than a login error
    void restoreSession(FetchProgressCallback onFetchProgress,
                        std::function<void(Result<void>)> onDone);

    // Persists the session token on success, best-effort: failing to persist doesn't
    // fail the login. kEMfaRequired means 2FA, and the caller must hold the
    // credentials and retry via loginWithTwoFactor -- this class doesn't special-case
    // that code.
    void login(const std::string& email,
               const std::string& password,
               FetchProgressCallback onFetchProgress,
               std::function<void(Result<void>)> onDone);

    // Resubmits email/password alongside a 2FA pin, after login() failed
    // with kEMfaRequired. Same success/persistence behavior as login().
    void loginWithTwoFactor(const std::string& email,
                            const std::string& password,
                            const std::string& pin,
                            FetchProgressCallback onFetchProgress,
                            std::function<void(Result<void>)> onDone);

    // onDone's Result is always ok(): MegaApi::logout's docs say API_ESID is not an
    // error here, and failing to reach the server shouldn't strand the user. The
    // persisted token is cleared either way.
    void logout(std::function<void(Result<void>)> onDone);

private:
    // Shared tail of every successful auth: fetch the node tree, then persist the
    // token. A fetchNodes failure that is itself definitively-invalid (the account
    // got blocked between auth and fetch) also clears the stored session.
    void finishLoginSuccess(FetchProgressCallback onFetchProgress,
                            std::function<void(Result<void>)> onDone);

    std::shared_ptr<IMegaClient> mClient;
    std::shared_ptr<ISessionStore> mSessionStore;
};
