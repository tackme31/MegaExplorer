#pragma once
#include "FileEntry.h"
#include "Result.h"

#include <cstdint>
#include <vector>

// Local persistent cache of the MEGA node tree, keyed per parent folder.
// Synchronous by design, unlike IMegaClient: IMegaClient's callback shape
// exists to stay consistent with genuinely-async SDK calls (login,
// fetchNodes, download, getThumbnail) that share its interface -- INodeCache
// has no such sibling, local SQLite I/O is inherently synchronous, and a
// callback shape here would just add indirection for no benefit.
//
// Implementations must never throw; every failure mode (missing file,
// corrupt DB, I/O error) surfaces as Result::fail. loadChildren does not
// distinguish "never cached" from "cached as empty" -- both return
// Result::ok with an empty vector. The policy of treating an empty result as
// "nothing to show yet" belongs to the caller (FolderNavigationService),
// not this store: see its loadWithCache helper.
class INodeCache
{
public:
    virtual ~INodeCache() = default;

    // Identifies the folder whose children are being cached/looked up. Same
    // isRoot-sentinel convention as FolderNavigationService::Location
    // (handle is meaningless when isRoot is true).
    struct ParentKey
    {
        bool isRoot = true;
        std::uint64_t handle = 0;
    };

    virtual Result<std::vector<FileEntry>> loadChildren(const ParentKey& parent) const = 0;

    // Full-replace-per-parent: deletes every previously cached row for
    // `parent`, then inserts `entries`, transactionally (all-or-nothing). No
    // incremental diffing -- an authoritative network result always fully
    // replaces whatever was cached for that folder.
    virtual Result<void> saveChildren(const ParentKey& parent,
                                      const std::vector<FileEntry>& entries) = 0;
};
