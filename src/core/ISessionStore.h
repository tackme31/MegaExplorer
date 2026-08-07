#pragma once
#include "Result.h"

#include <string>

// Persists one MEGA session token across app restarts, encrypted at rest.
// Synchronous by design: local encrypted-file I/O is inherently so.
//
// Implementations must never throw -- every failure surfaces as Result::fail. A real
// token is never legitimately empty, so loadSession uses "" as an unambiguous
// "nothing stored" sentinel, distinct from a read/decrypt failure.
class ISessionStore
{
public:
    virtual ~ISessionStore() = default;

    virtual Result<std::string> loadSession() const = 0;
    virtual Result<void> saveSession(const std::string& sessionToken) = 0;
    virtual Result<void> clearSession() = 0;
};
