#pragma once
#include "Result.h"

#include <string>

// Persists a single MEGA session token (as returned by MegaApi::dumpSession) across
// app restarts, encrypted at rest. Synchronous by design, same rationale as
// INodeCache: no genuinely-async sibling to stay consistent with, and local
// encrypted-file I/O is inherently synchronous.
//
// Implementations must never throw; every failure mode (I/O error, decrypt failure)
// surfaces as Result::fail. A real session token is never legitimately an empty
// string, so loadSession uses "" as an unambiguous "nothing stored" sentinel, kept
// distinguishable from a genuine read/decrypt failure (Result::fail) -- callers
// decide whether to treat both the same way.
class ISessionStore
{
public:
    virtual ~ISessionStore() = default;

    virtual Result<std::string> loadSession() const = 0;
    virtual Result<void> saveSession(const std::string& sessionToken) = 0;
    virtual Result<void> clearSession() = 0;
};
