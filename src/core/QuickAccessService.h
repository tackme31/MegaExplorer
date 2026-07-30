#pragma once
#include "IMegaClient.h"
#include "IPinnedFolderStore.h"
#include "PinnedFolder.h"

#include <cstdint>
#include <functional>
#include <memory>
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
    // worth blocking startup over.
    void load();

    const std::vector<PinnedFolder>& pins() const;

    bool isPinned(std::uint64_t handle) const;

    // Appends to the end -- pin order is insertion order, there's no
    // reordering UI. Returns false (and writes nothing) if handle is already
    // pinned.
    bool pin(const PinnedFolder& folder);

    // Returns false (and writes nothing) if handle isn't pinned.
    bool unpin(std::uint64_t handle);

    // Wholesale replacement, for committing the login-time validation sweep's
    // result in one write instead of one per dropped/renamed pin.
    void replaceAll(std::vector<PinnedFolder> pins);

    // Sign-out: drops the in-memory list but deliberately leaves the store
    // alone, so signing back into the same account restores the pins. (A
    // different account's pins can't survive the next validation sweep
    // anyway -- its handles don't resolve.)
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
};
