#pragma once
#include "IMegaClient.h"
#include "IPinnedFolderStore.h"
#include "PinnedFolder.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Owns the quick-access pin list and writes it through to IPinnedFolderStore
// on every mutation. Qt-free (MegaExplorerCore) like the other *Service
// classes, so it's testable against MockMegaClient/MockPinnedFolderStore
// without a GUI.
//
// Deliberately synchronous apart from resolveFolder, which is a thin
// pass-through to IMegaClient: every mutator here runs on the caller's thread
// and touches mPins directly. Driving the login-time validation sweep -- N
// concurrent resolveFolder calls whose results have to be marshaled back onto
// the GUI thread and reconciled -- is QuickAccessModel's job, matching the
// FolderTreeService/FolderTreeModel split (services pass through, src/qml
// marshals). Putting it here instead would mean mutating mPins from the SDK
// thread while the GUI thread reads it.
class QuickAccessService
{
public:
    QuickAccessService(std::shared_ptr<IMegaClient> client,
                       std::shared_ptr<IPinnedFolderStore> store);

    // Reads the persisted list into memory as-is, without validating any pin
    // against the node tree. A load failure degrades to an empty list (the
    // adapter has already logged the cause) -- a broken pin list is never
    // worth blocking startup over. Also resolves and caches the current
    // account's key (Phase 11a, via IMegaClient::currentUserHandle) so every
    // subsequent mutator persists under that account's own storage slot; if
    // the client isn't logged in, the account key is cleared instead and the
    // list degrades to empty, same as a load failure.
    void load();

    const std::vector<PinnedFolder>& pins() const;

    bool isPinned(std::uint64_t handle) const;

    // Appends to the end. Returns false (and writes nothing) if handle is
    // already pinned. Reordering afterwards is move()'s job (Phase 22a).
    bool pin(const PinnedFolder& folder);

    // Returns false (and writes nothing) if handle isn't pinned.
    bool unpin(std::uint64_t handle);

    // Moves one pin so that it ends up *at* index `to` in the resulting list
    // -- i.e. `to` is a final position, not an insertion point in the old
    // coordinates. Returns false (and writes nothing) for an out-of-range
    // index or a no-op move, same "rejected means unpersisted" rule as
    // pin()/unpin().
    bool move(std::size_t from, std::size_t to);

    // Wholesale replacement, for committing the login-time validation sweep's
    // result in one write instead of one per dropped/renamed pin.
    void replaceAll(std::vector<PinnedFolder> pins);

    // Sign-out: drops the in-memory list and the cached account key, but
    // deliberately leaves the store alone -- signing back into the same
    // account restores its pins via the next load(), which re-resolves the
    // key (Phase 11a: a different account now gets its own storage slot
    // instead of colliding with this one).
    void clear();

    void resolveFolder(std::uint64_t handle, std::function<void(Result<NodeInfo>)> onDone);

    // The single definition of "this pin still points at something usable",
    // shared by the login-time sweep and the click-time check. A deleted node
    // is only moved to the Rubbish bin, so it keeps resolving successfully --
    // inCloud is what actually rules it out.
    static bool isUsable(const Result<NodeInfo>& resolved);

private:
    std::vector<PinnedFolder>::const_iterator find(std::uint64_t handle) const;

    std::shared_ptr<IMegaClient> mClient;
    std::shared_ptr<IPinnedFolderStore> mStore;
    std::vector<PinnedFolder> mPins;
    // Empty when not logged in / load() hasn't resolved an account yet.
    // Set by load(), consumed by pin()/unpin()/replaceAll() to scope
    // mStore->save() (Phase 11a).
    std::string mAccountKey;
};
