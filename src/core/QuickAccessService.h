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

// Owns the quick-access pin list and writes it through to IPinnedFolderStore on
// every mutation.
//
// Deliberately synchronous apart from resolveFolder: every mutator runs on the
// caller's thread and touches mPins directly. Driving the login-time validation
// sweep -- N concurrent resolves whose results must be marshalled back onto the
// GUI thread -- is QuickAccessModel's job. Doing it here would mean mutating mPins
// from the SDK thread while the GUI thread reads it.
class QuickAccessService
{
public:
    QuickAccessService(std::shared_ptr<IMegaClient> client,
                       std::shared_ptr<IPinnedFolderStore> store);

    // Reads the persisted list as-is, without validating any pin against the node
    // tree. A load failure degrades to an empty list and says why in the Result,
    // which is the caller's only chance to log it (this class is Qt-free). The next
    // mutator then overwrites the unreadable data: corrupt stored JSON isn't
    // recoverable from inside the app, and refusing to save would silently throw
    // away everything the user pins from then on.
    //
    // Also caches the current account's key, so every later mutator persists under
    // that account's own slot; not being logged in clears it and empties the list.
    Result<void> load();

    const std::vector<PinnedFolder>& pins() const;

    bool isPinned(std::uint64_t handle) const;

    // Appends. Returns false, writing nothing, if handle is already pinned.
    bool pin(const PinnedFolder& folder);

    // Returns false (and writes nothing) if handle isn't pinned.
    bool unpin(std::uint64_t handle);

    // `to` is the final position, not an insertion point in the old coordinates.
    // Out-of-range and no-op moves return false and write nothing.
    bool move(std::size_t from, std::size_t to);

    // Wholesale replacement, for committing the login-time validation sweep's
    // result in one write instead of one per dropped/renamed pin.
    void replaceAll(std::vector<PinnedFolder> pins);

    // Sign-out: drops the in-memory list and the account key but deliberately leaves
    // the store alone, so signing back in restores that account's pins.
    void clear();

    void resolveFolder(std::uint64_t handle, std::function<void(Result<NodeInfo>)> onDone);

    // What a resolveFolder result says about the pin that produced it.
    enum class PinStatus
    {
        // A live folder in the Cloud Drive.
        Usable,
        // Definitively unusable: a file, a node in the Rubbish bin, or a handle
        // that doesn't resolve at all.
        Gone,
        // The question couldn't be answered. Says nothing about the target, so
        // the pin has to be left alone.
        Unknown,
    };

    // The single definition of "this pin still points at something usable". A
    // deleted node keeps resolving from the Rubbish bin, so inCloud is what actually
    // rules it out.
    //
    // Gone and Unknown are kept apart because the sweep *deletes* on Gone: as one
    // bool, a shutdown mid-sweep failed every resolve, marked every pin dangling and
    // wrote the emptied list to disk. The definitive codes are therefore an
    // allowlist and anything unrecognized falls through to Unknown -- same shape as
    // isSessionDefinitivelyInvalid. Wrongly keeping a dead pin costs one dialog;
    // wrongly dropping a live one is unrecoverable.
    static PinStatus classify(const Result<NodeInfo>& resolved);

    // Where a failed write-through goes. Called synchronously from inside the failing
    // mutator, on the GUI thread, so the handler needs no marshalling. It means "the
    // in-memory list is updated but won't survive a restart" -- whether the change
    // was accepted at all is the mutator's own bool return.
    void setOnPersistenceFailed(std::function<void(const Result<void>&)> handler);

private:
    // Single write-through point for every mutator, so Result's [[nodiscard]] is
    // handled in one place rather than four.
    void persist();

    std::vector<PinnedFolder>::const_iterator find(std::uint64_t handle) const;

    std::shared_ptr<IMegaClient> mClient;
    std::shared_ptr<IPinnedFolderStore> mStore;
    std::function<void(const Result<void>&)> mOnPersistenceFailed;
    std::vector<PinnedFolder> mPins;
    // Empty until load() resolves an account. Scopes every mStore->save().
    std::string mAccountKey;
};
