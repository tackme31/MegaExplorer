#pragma once
#include "IMegaClient.h"

#include <functional>
#include <memory>
#include <string>

// Reads the signed-in account's profile and storage quota for the account
// section of the "More" menu. SDK-free like the rest of src/core,
// unit-testable with a mocked IMegaClient.
//
// Deliberately NOT part of AuthService: that class documents (see its own
// header) that post-login state belongs to the caller, not to it, so auth
// stays independently testable and nothing races to reset the same state.
// Loading a profile after login is that same category of side effect.
//
// Deliberately stateless -- one member, no cache, no mutex. This is the
// opposite of ThumbnailService, and the difference is not an oversight: that
// class caches and dedupes because a scrolling grid fires dozens of requests
// for *different* handles, whereas there is exactly one account here, at most
// one request of each kind in flight, and no handle to key a cache by. The
// "fetch once per session" rule lives in the caller (AccountController), and
// the retry path must re-issue, so a cache here would be actively wrong.
class AccountService
{
public:
    explicit AccountService(std::shared_ptr<IMegaClient> client);

    // Straight pass-through to IMegaClient::currentAccountIdentity -- present
    // so callers depend only on this class. Synchronous; fails when not
    // logged in.
    Result<AccountIdentity> identity() const;

    // Resolves the account's display name, or an empty string when it has
    // none. Takes no Result: a MEGA account is not required to have a name
    // set, so "no name" is an ordinary answer rather than a failure, and the
    // UI is required to degrade to showing the email alone. onDone fires
    // exactly once whichever way the two underlying reads go.
    //
    // The two attribute requests are issued *sequentially* (last name from
    // within first name's callback), not in parallel. In parallel the join
    // accumulator would be written from two SDK background threads, needing a
    // mutex plus a both-settled counter -- and taking a lock around a call
    // whose callback may fire synchronously is exactly the self-deadlock
    // ThumbnailService's header warns about, which MockMegaClient reproduces.
    // Sequentially there is no shared mutable state at all and the test is
    // deterministic. The cost is one extra serial round-trip for two tiny
    // reads, on a path that has just finished waiting on fetchNodes.
    void loadDisplayName(std::function<void(std::string)> onDone);

    // Downloads the account's avatar to destinationPath (exact path, resolved
    // by the caller, same division as IMegaClient::getThumbnail).
    //
    // Never reports failure. Most accounts have no avatar, so *any* error
    // becomes AvatarOutcome{hasAvatar = false} carrying the reason for
    // logging. This is deliberately not keyed on a specific error code:
    // megaapi.h does not document which code the no-avatar case produces, the
    // UI has no error affordance for avatars anyway (it falls back to a
    // coloured initial unconditionally), and so there is nothing a caller
    // could usefully do differently.
    void loadAvatar(const std::string& destinationPath,
                    std::function<void(Result<AvatarOutcome>)> onDone);

    // Storage quota and plan level. A real server round-trip -- see
    // IMegaClient::getAccountInfo. Failures are reported, because this is the
    // one part of the account section with a visible error state.
    void loadAccountInfo(std::function<void(Result<AccountInfo>)> onDone);

private:
    std::shared_ptr<IMegaClient> mClient;
};
