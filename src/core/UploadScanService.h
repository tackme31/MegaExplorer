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
    std::string localPath;          // the local file that would land on a taken name
    std::string name;               // leaf name, shared by both sides
    std::uint64_t parentHandle = 0; // MEGA folder it would land in
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
// The walk only descends where both sides have a folder of the same name, so its
// cost tracks the size of the overlap rather than the size of the upload: adding
// one file to a local copy of a 100-file folder costs one directory listing and
// two MEGA lookups. Both lookups answer from memory or local SQLite, so a scan
// never goes to the network (spec 1-1).
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
    Result<std::vector<UploadPlanItem>> planSkippingCollisions(
        const std::vector<std::string>& localPaths,
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
    };

    Result<Scan> scan(const std::vector<std::string>& localPaths,
                      std::uint64_t parentHandle,
                      bool parentIsRoot) const;

    Result<void> scanLevel(const std::vector<LocalEntry>& entries,
                           std::uint64_t parentHandle,
                           bool parentIsRoot,
                           int depth,
                           Scan& out) const;

    // Fails when a folder it has to take apart can no longer be listed: the
    // survivors inside it would otherwise be dropped from the plan without a
    // trace, and "skip" would then quietly upload less than the user agreed to.
    Result<void> addToPlan(const LocalEntry& entry,
                           std::uint64_t parentHandle,
                           bool parentIsRoot,
                           const Scan& scanned,
                           const std::set<std::string>& collided,
                           std::vector<UploadPlanItem>& plan) const;

    std::shared_ptr<IMegaClient> mClient;
    std::shared_ptr<ILocalFileSystem> mFs;
};
