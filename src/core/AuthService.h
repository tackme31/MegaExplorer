#pragma once
#include "IMegaClient.h"
#include "ISessionStore.h"
#include "Result.h"

#include <functional>
#include <memory>
#include <string>

// Sentinel errorCode for restoreSession's onDone when ISessionStore reports
// "nothing stored" (loadSession() returned "" successfully). Distinct from
// every mega::MegaError value mirrored in MegaErrorCodes.h, all of which are
// <= 0, so it can never collide with a real SDK error code.
constexpr int kNoStoredSession = 1;

// Whether errorCode means a stored session is definitively unusable -- i.e.
// safe to clearSession() without risking discarding a session that would
// have worked once a merely-transient condition (offline, server hiccup)
// passes. Unknown codes are treated as transient (false): the cost of
// keeping a session that turns out to be genuinely dead is one spurious
// restoreSession failure at next launch, versus wrongly discarding a good
// session forces the user to re-enter their password. Shared between
// AuthService (restoreSession) and the QML-facing AuthController (Part 2 of
// this feature) so both layers agree on the same classification.
bool isSessionDefinitivelyInvalid(int errorCode);

// Coordinates login/logout and session-token persistence. Sits above
// IMegaClient/ISessionStore, both injected -- SDK-free like the rest of
// src/core, unit-testable with mocks of both.
//
// Deliberately does not depend on FolderNavigationService: resetting
// navigation state on logout/re-login is the caller's responsibility (Part
// 2: FolderNavigationController::reset), not this class's -- keeps auth and
// navigation independently testable and avoids two callers racing to reset
// the same state.
class AuthService
{
public:
    AuthService(std::shared_ptr<IMegaClient> client, std::shared_ptr<ISessionStore> sessionStore);

    // Attempts to resume a previously persisted session. onDone's Result:
    //   - success -> already logged in and nodes fetched, ready to use.
    //   - fail, errorCode == kNoStoredSession -> nothing was ever saved; not
    //     an error, just "show the login screen" silently.
    //   - fail, isSessionDefinitivelyInvalid(errorCode) -> the stored
    //     session was actively rejected; already cleared by the time onDone
    //     fires.
    //   - fail, any other errorCode -> transient failure (e.g. offline); the
    //     stored session was left untouched, caller should surface a
    //     "couldn't connect" message rather than a login error.
    void restoreSession(std::function<void(Result<void>)> onDone);

    // Fresh email/password login. On success, persists the resulting
    // session token (best-effort -- failure to persist doesn't fail the
    // login itself). On failure with errorCode ==
    // MegaErrorCode::kEMfaRequired, the account has 2FA enabled; the caller
    // is expected to hold onto email/password and retry via
    // loginWithTwoFactor. AuthService itself does not special-case that
    // code -- interpreting it is the caller's (AuthController's) job.
    void login(const std::string& email,
               const std::string& password,
               std::function<void(Result<void>)> onDone);

    // Resubmits email/password alongside a 2FA pin, after login() failed
    // with kEMfaRequired. Same success/persistence behavior as login().
    void loginWithTwoFactor(const std::string& email,
                            const std::string& password,
                            const std::string& pin,
                            std::function<void(Result<void>)> onDone);

    // Always succeeds from the caller's perspective (onDone's Result is
    // always ok()) -- MegaApi::logout's own documentation says API_ESID
    // should not be treated as an error here, and a local-only failure to
    // reach the server shouldn't block the user from getting back to the
    // login screen. Clears the persisted session token regardless of
    // whether the server round-trip succeeded.
    void logout(std::function<void(Result<void>)> onDone);

private:
    // Shared tail of restoreSession/login/loginWithTwoFactor once the SDK
    // reports a successful auth: fetches the node tree, then best-effort
    // persists the new session token. A fetchNodes failure that is itself
    // definitively-invalid (e.g. the account got blocked between auth and
    // fetch) also clears any previously stored session.
    void finishLoginSuccess(std::function<void(Result<void>)> onDone);

    std::shared_ptr<IMegaClient> mClient;
    std::shared_ptr<ISessionStore> mSessionStore;
};
