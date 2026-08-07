#pragma once
#include "PinnedFolder.h"
#include "Result.h"

#include <string>
#include <vector>

// Persists the quick-access pin list across app restarts, in order. Synchronous by
// design, like ISessionStore.
//
// Implementations must never throw -- every failure surfaces as Result::fail. Unlike
// ISessionStore there is no sentinel-vs-failure subtlety: an empty vector is the
// legitimate "nothing pinned yet" value, and a load failure is treated the same way.
//
// accountKey scopes the list to one MEGA account -- the user handle as a decimal
// string, not an email, so it is stable and needs no escaping. Without it, switching
// accounts would read and overwrite the previous account's pins.
class IPinnedFolderStore
{
public:
    virtual ~IPinnedFolderStore() = default;

    virtual Result<std::vector<PinnedFolder>> load(const std::string& accountKey) const = 0;
    virtual Result<void> save(const std::string& accountKey,
                              const std::vector<PinnedFolder>& pins) = 0;
};
