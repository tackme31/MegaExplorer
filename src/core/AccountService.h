#pragma once
#include "IMegaClient.h"

#include <functional>
#include <memory>
#include <string>

// Reads the signed-in account's profile and storage quota.
//
// Deliberately stateless -- no cache, no mutex, unlike ThumbnailService: that
// class caches because a scrolling grid fires dozens of requests for *different*
// handles, whereas there is one account here and no handle to key on. "Fetch once
// per session" lives in AccountController, and the retry path must re-issue, so a
// cache here would be actively wrong.
class AccountService
{
public:
    explicit AccountService(std::shared_ptr<IMegaClient> client);

    // Pass-through, present so callers depend only on this class.
    Result<AccountIdentity> identity() const;

    // Resolves the display name, empty when the account has none. Takes no Result:
    // a MEGA account need not have a name, so that is an ordinary answer and the UI
    // degrades to the email alone.
    //
    // The two attribute requests are issued *sequentially*, last name from within
    // first name's callback. In parallel the join accumulator would be written from
    // two SDK threads, needing a mutex plus a both-settled counter -- and locking
    // around a call whose callback may fire synchronously is the self-deadlock
    // ThumbnailService's header warns about. The cost is one extra serial round-trip.
    void loadDisplayName(std::function<void(std::string)> onDone);

    // Never reports failure: most accounts have no avatar, so *any* error becomes
    // AvatarOutcome{hasAvatar = false} carrying the reason for logging. Not keyed on
    // a specific code -- megaapi.h doesn't document which one the no-avatar case
    // produces, and the UI falls back to a coloured initial regardless.
    void loadAvatar(const std::string& destinationPath,
                    std::function<void(Result<AvatarOutcome>)> onDone);

    // A real server round-trip. Failures are reported, because this is the one part
    // of the account section with a visible error state.
    void loadAccountInfo(std::function<void(Result<AccountInfo>)> onDone);

    // Whether an overwritten file keeps its previous content as a version. Takes no
    // Result: an account that never touched the setting has the attribute unset, and
    // unset means enabled, so "no answer" and "enabled" are the same state. Any other
    // failure degrades to enabled too -- that is the SDK's own default, and the
    // alternative is a third state every caller would have to word
    // (docs/investigations/SPEC_NAME_CONFLICT_COPY_MOVE.md 1-3).
    void loadFileVersioningEnabled(std::function<void(bool)> onDone);

private:
    std::shared_ptr<IMegaClient> mClient;
};
