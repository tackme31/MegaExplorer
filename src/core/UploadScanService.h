#pragma once
#include "FileEntry.h"
#include "ILocalFileSystem.h"
#include "IMegaClient.h"

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

struct UploadCollision
{
    std::string localPath; // the local file that would land on a taken name
    std::string name;      // leaf name, shared by both sides
    // MEGA folder it would land in, or 0 when that folder does not exist yet
    // because this same upload creates it.
    std::uint64_t parentHandle = 0;
    bool parentIsRoot = false;
    // The MEGA file already sitting there, or 0 when the name is taken by an
    // earlier file in this same upload rather than by anything on MEGA.
    std::uint64_t existingHandle = 0;
};

// One upload the app issues itself: a local path -- a file, or a whole directory
// the SDK then transfers recursively -- and the MEGA folder it lands in.
struct UploadPlanItem
{
    std::string localPath;
    std::uint64_t parentHandle = 0;
    bool parentIsRoot = false;
};

// Answers "which files in this upload land on a name already spoken for",
// including the ones nested inside folders being uploaded, since the SDK merges a
// folder into a same-named one instead of refusing (SPEC_NAME_CONFLICT_UPLOAD.md
// 1-2). A name the upload itself brings twice counts too: the second copy versions
// over the first exactly as it would over a pre-existing node.
//
// The walk descends where both sides have a folder of the same name, and where the
// upload itself brings one name twice -- the SDK merges those two into one folder
// as well, so their contents land on each other. Every copy of a name is walked as
// a single level, since that is what they become on MEGA. The cost still tracks the
// size of the overlap rather than the size of the upload: adding one file to a local
// copy of a 100-file folder costs one directory listing and two MEGA lookups. Both
// lookups answer from memory or local SQLite, so a scan never goes to the network
// (spec 1-1).
class UploadScanService
{
public:
    UploadScanService(std::shared_ptr<IMegaClient> client, std::shared_ptr<ILocalFileSystem> fs);

    // Local paths that no longer exist are skipped rather than failing the scan:
    // the upload itself drops them the same way. A failed MEGA lookup, or a tree
    // deeper than kMaxDepth, fails the whole scan instead of answering short --
    // a caller has to be able to tell "no collisions" from "could not tell".
    Result<std::vector<UploadCollision>> findCollisions(const std::vector<std::string>& localPaths,
                                                        std::uint64_t parentHandle,
                                                        bool parentIsRoot) const;

    // What "skip the duplicates" has to be turned into. The SDK's recursive upload
    // has no per-file hook (spec 1-2), so a branch holding a collision is walked
    // here and its surviving parts issued one by one, while a branch holding none
    // is handed over whole and left to the SDK. Fails wherever findCollisions
    // would, and additionally when a branch it has to take apart can no longer be
    // listed: a caller must not treat "could not tell" as "nothing collides" here,
    // since uploading everything is what the user just declined.
    Result<std::vector<UploadPlanItem>>
    planSkippingCollisions(const std::vector<std::string>& localPaths,
                           std::uint64_t parentHandle,
                           bool parentIsRoot) const;

    // Directory junctions can make a local tree cyclic, and Windows resolves them
    // transparently, so the walk needs a stop of its own. Reaching it is reported
    // as a failure (kEInternal), never as an empty result.
    static constexpr int kMaxDepth = 32;

private:
    struct Scan
    {
        std::vector<LocalEntry> topLevel; // the dropped paths that still exist
        std::vector<UploadCollision> collisions;
        // MEGA handle of every local directory the walk descended into. The walk is
        // the only place those lookups happen, and the skip plan needs them: a file
        // that survives beside a colliding one has to name the folder they share.
        std::map<std::string, std::uint64_t> folderHandles;
        // Local directories the walk descended into that MEGA has no folder for --
        // this upload creates it. Having no handle, they cannot be named as a plan
        // item's parent, which is what limits what "skip" can do inside them.
        std::set<std::string> createdFolders;
    };

    Result<Scan> scan(const std::vector<std::string>& localPaths,
                      std::uint64_t parentHandle,
                      bool parentIsRoot) const;

    // parentOnMega false means the folder holding this level is one the upload
    // creates, so nothing on MEGA can be there to collide with and neither lookup
    // is worth making -- only the names the batch itself brings twice count.
    Result<void> scanLevel(const std::vector<LocalEntry>& entries,
                           std::uint64_t parentHandle,
                           bool parentIsRoot,
                           bool parentOnMega,
                           int depth,
                           Scan& out) const;

    // Fails when a folder it has to take apart can no longer be listed: the
    // survivors inside it would otherwise be dropped from the plan without a
    // trace, and "skip" would then quietly upload less than the user agreed to.
    // Fails too when a folder the upload creates itself has to be taken apart and
    // something in it survives, since a plan item can only name a parent that
    // already has a handle; a copy of which nothing survives is simply left out.
    Result<void> addToPlan(const LocalEntry& entry,
                           std::uint64_t parentHandle,
                           bool parentIsRoot,
                           const Scan& scanned,
                           const std::set<std::string>& collided,
                           std::vector<UploadPlanItem>& plan) const;

    // Whether any file under `entry` would still be uploaded. Asked only about a
    // copy of a folder this upload creates itself, where the answer decides between
    // leaving the whole copy out and refusing to plan: no part of it can be named.
    Result<bool> anySurvivorUnder(const LocalEntry& entry,
                                  const Scan& scanned,
                                  const std::set<std::string>& collided,
                                  int depth) const;

    std::shared_ptr<IMegaClient> mClient;
    std::shared_ptr<ILocalFileSystem> mFs;
};
