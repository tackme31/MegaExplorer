#pragma once
#include "FileEntry.h"
#include "ILocalFileSystem.h"
#include "IMegaClient.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct UploadCollision
{
    std::string localPath;          // the local file that would land on an existing one
    std::string name;               // leaf name, shared by both sides
    std::uint64_t parentHandle = 0; // MEGA folder it would land in
    bool parentIsRoot = false;
    std::uint64_t existingHandle = 0; // the MEGA file already sitting there
};

// Answers "which files in this upload already exist on MEGA", including the ones
// nested inside folders being uploaded, since the SDK merges a folder into a
// same-named one instead of refusing (SPEC_NAME_CONFLICT_UPLOAD.md 1-2).
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

    // Directory junctions can make a local tree cyclic, and Windows resolves them
    // transparently, so the walk needs a stop of its own. Reaching it is reported
    // as a failure (kEInternal), never as an empty result.
    static constexpr int kMaxDepth = 32;

private:
    Result<void> scanLevel(const std::vector<LocalEntry>& entries,
                           std::uint64_t parentHandle,
                           bool parentIsRoot,
                           int depth,
                           std::vector<UploadCollision>& out) const;

    std::shared_ptr<IMegaClient> mClient;
    std::shared_ptr<ILocalFileSystem> mFs;
};
