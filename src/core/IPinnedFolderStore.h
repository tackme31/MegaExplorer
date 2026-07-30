#pragma once
#include "PinnedFolder.h"
#include "Result.h"

#include <string>
#include <vector>

// Persists the quick-access pin list across app restarts, in order.
// Synchronous by design, same rationale as ISessionStore: no genuinely-async
// sibling to stay consistent with, and local settings I/O is inherently
// synchronous.
//
// Implementations must never throw; every failure mode (I/O error, corrupt
// stored data) surfaces as Result::fail. Unlike ISessionStore::loadSession
// there's no sentinel-vs-failure subtlety to resolve: an empty vector is the
// legitimate "nothing pinned yet" value, and callers already treat a load
// failure the same way (start with no pins), so QuickAccessService doesn't
// distinguish them.
//
// accountKey (Phase 11a) scopes the pin list to one MEGA account: the current
// account's user handle rendered as a decimal string (see
// IMegaClient::currentUserHandle), not an email -- stable and needs no
// escaping. Without this, switching accounts would read/overwrite the
// previous account's pins (Phase 11's original, machine-wide design).
class IPinnedFolderStore
{
public:
    virtual ~IPinnedFolderStore() = default;

    virtual Result<std::vector<PinnedFolder>> load(const std::string& accountKey) const = 0;
    virtual Result<void> save(const std::string& accountKey,
                              const std::vector<PinnedFolder>& pins) = 0;
};
