#include "UploadScanService.h"

#include "MegaErrorCodes.h"

#include <map>
#include <utility>

namespace
{
// First hit per name wins: MEGA lets siblings share a name, so a lookup can come
// back with two nodes called "a.txt" -- only one of which the SDK would version
// over -- and the walk must not turn that into two answers about one local file.
std::map<std::string, std::uint64_t> handlesByName(const std::vector<FileEntry>& hits)
{
    std::map<std::string, std::uint64_t> byName;
    for (const FileEntry& hit : hits)
        byName.emplace(hit.name, hit.handle);
    return byName;
}

// Collision paths are absolute and native, and this is a Windows-only app, so
// "inside this directory" is a '\' prefix test. Ordered set, hence lower_bound
// rather than a scan.
bool hasCollisionUnder(const std::set<std::string>& collided, const std::string& directory)
{
    const std::string prefix = directory + "\\";
    const auto it = collided.lower_bound(prefix);
    return it != collided.end() && it->compare(0, prefix.size(), prefix) == 0;
}
} // namespace

UploadScanService::UploadScanService(std::shared_ptr<IMegaClient> client,
                                     std::shared_ptr<ILocalFileSystem> fs)
    : mClient(std::move(client)), mFs(std::move(fs))
{}

Result<std::vector<UploadCollision>> UploadScanService::findCollisions(
    const std::vector<std::string>& localPaths, std::uint64_t parentHandle, bool parentIsRoot) const
{
    Result<Scan> scanned = scan(localPaths, parentHandle, parentIsRoot);
    if (!scanned.success)
        return Result<std::vector<UploadCollision>>::fail(scanned.errorMessage, scanned.errorCode);
    return Result<std::vector<UploadCollision>>::ok(std::move(scanned.value().collisions));
}

Result<std::vector<UploadPlanItem>> UploadScanService::planSkippingCollisions(
    const std::vector<std::string>& localPaths, std::uint64_t parentHandle, bool parentIsRoot) const
{
    Result<Scan> scanned = scan(localPaths, parentHandle, parentIsRoot);
    if (!scanned.success)
        return Result<std::vector<UploadPlanItem>>::fail(scanned.errorMessage, scanned.errorCode);

    std::set<std::string> collided;
    for (const UploadCollision& hit : scanned.value().collisions)
        collided.insert(hit.localPath);

    std::vector<UploadPlanItem> plan;
    for (const LocalEntry& entry : scanned.value().topLevel)
    {
        Result<void> added =
            addToPlan(entry, parentHandle, parentIsRoot, scanned.value(), collided, plan);
        if (!added.success)
            return Result<std::vector<UploadPlanItem>>::fail(added.errorMessage, added.errorCode);
    }
    return Result<std::vector<UploadPlanItem>>::ok(std::move(plan));
}

Result<void> UploadScanService::addToPlan(const LocalEntry& entry,
                                          std::uint64_t parentHandle,
                                          bool parentIsRoot,
                                          const Scan& scanned,
                                          const std::set<std::string>& collided,
                                          std::vector<UploadPlanItem>& plan) const
{
    if (!entry.isDirectory)
    {
        if (collided.find(entry.path) == collided.end())
            plan.push_back(UploadPlanItem{entry.path, parentHandle, parentIsRoot});
        return Result<void>::ok();
    }

    // Only a directory that actually holds a live collision is worth taking apart --
    // everything else goes up as a single SDK folder transfer.
    const auto merged = scanned.folderHandles.find(entry.path);
    if (!hasCollisionUnder(collided, entry.path) ||
        (merged == scanned.folderHandles.end() && scanned.createdFolders.count(entry.path) == 0))
    {
        plan.push_back(UploadPlanItem{entry.path, parentHandle, parentIsRoot});
        return Result<void>::ok();
    }

    if (merged == scanned.folderHandles.end())
    {
        // A folder this upload creates itself has no handle, so nothing inside it
        // can be named as a plan item's parent: this copy is either wholly
        // redundant, and left out, or it holds something that cannot be addressed
        // at all. Sending it whole in that second case would version over exactly
        // what Skip was asked to spare, so the plan says it cannot be built.
        Result<bool> survivor = anySurvivorUnder(entry, scanned, collided, 0);
        if (!survivor.success)
            return Result<void>::fail(survivor.errorMessage, survivor.errorCode);
        if (!survivor.value())
            return Result<void>::ok();
        return Result<void>::fail("\"" + entry.name +
                                      "\" is uploaded twice into a folder MEGA does not have yet, "
                                      "so its duplicates cannot be left out one by one",
                                  MegaErrorCode::kEInternal);
    }

    // The scan listed this folder a moment ago, so a refusal here is a race (it
    // was removed, or locked) rather than the ordinary case -- and one that would
    // silently shrink the upload, hence a failure instead of an empty level.
    const std::optional<std::vector<LocalEntry>> children = mFs->listDirectory(entry.path);
    if (!children)
        return Result<void>::fail("Could not read the local folder \"" + entry.name + "\"",
                                  MegaErrorCode::kEInternal);
    for (const LocalEntry& child : *children)
    {
        Result<void> deeper = addToPlan(child, merged->second, false, scanned, collided, plan);
        if (!deeper.success)
            return deeper;
    }
    return Result<void>::ok();
}

Result<bool> UploadScanService::anySurvivorUnder(const LocalEntry& entry,
                                                 const Scan& scanned,
                                                 const std::set<std::string>& collided,
                                                 int depth) const
{
    if (!entry.isDirectory)
        return Result<bool>::ok(collided.find(entry.path) == collided.end());
    // This walk reaches parts of the tree the scan had no reason to enter, so it
    // needs the same stop against a cyclic directory junction.
    if (depth >= kMaxDepth)
        return Result<bool>::fail("Local folders are nested too deeply to check for name "
                                  "collisions",
                                  MegaErrorCode::kEInternal);
    const std::optional<std::vector<LocalEntry>> children = mFs->listDirectory(entry.path);
    if (!children)
        return Result<bool>::fail("Could not read the local folder \"" + entry.name + "\"",
                                  MegaErrorCode::kEInternal);
    // An empty folder is still content, but only where no other copy in this upload
    // already brings it -- which is exactly what createdFolders records.
    if (children->empty())
        return Result<bool>::ok(scanned.createdFolders.count(entry.path) == 0);
    for (const LocalEntry& child : *children)
    {
        Result<bool> deeper = anySurvivorUnder(child, scanned, collided, depth + 1);
        if (!deeper.success || deeper.value())
            return deeper;
    }
    return Result<bool>::ok(false);
}

Result<UploadScanService::Scan> UploadScanService::scan(const std::vector<std::string>& localPaths,
                                                        std::uint64_t parentHandle,
                                                        bool parentIsRoot) const
{
    // One path listed twice would count as its own duplicate, and the skip plan
    // keys on path, so both copies would then be dropped instead of one. Keyed on
    // the resolved path rather than the caller's spelling, since that is the key
    // the plan uses: two spellings of one file differ here but not there.
    std::vector<LocalEntry> topLevel;
    topLevel.reserve(localPaths.size());
    std::set<std::string> seenPaths;
    for (const std::string& path : localPaths)
    {
        if (std::optional<LocalEntry> entry = mFs->entryFor(path))
        {
            if (seenPaths.insert(entry->path).second)
                topLevel.push_back(*entry);
        }
    }

    Scan scanned;
    Result<void> walk = scanLevel(topLevel, parentHandle, parentIsRoot, true, 0, scanned);
    if (!walk.success)
        return Result<Scan>::fail(walk.errorMessage, walk.errorCode);
    scanned.topLevel = std::move(topLevel);
    return Result<Scan>::ok(std::move(scanned));
}

Result<void> UploadScanService::scanLevel(const std::vector<LocalEntry>& entries,
                                          std::uint64_t parentHandle,
                                          bool parentIsRoot,
                                          bool parentOnMega,
                                          int depth,
                                          Scan& out) const
{
    // Two of the paths handed here can share a leaf name (dragged from different
    // folders), so each name is asked about once and the answer is paired back to
    // *every* path carrying it. Dropping the twin instead would leave its path out
    // of the skip plan, which keys on path -- and it would then upload over the
    // node the user just chose to spare.
    std::vector<const LocalEntry*> files;
    std::vector<std::string> fileNames;
    // Directories are grouped by name instead, in first-seen order: every copy of a
    // name becomes one folder on MEGA, so the level under them is walked once with
    // all their children together. Walking each copy on its own would leave the
    // files two copies bring in common invisible to each other.
    std::vector<std::string> directoryNames;
    std::map<std::string, std::vector<const LocalEntry*>> directoryCopies;
    for (const LocalEntry& entry : entries)
    {
        if (entry.isDirectory)
        {
            std::vector<const LocalEntry*>& copies = directoryCopies[entry.name];
            if (copies.empty())
                directoryNames.push_back(entry.name);
            copies.push_back(&entry);
            continue;
        }
        bool seen = false;
        for (const LocalEntry* known : files)
            seen = seen || known->name == entry.name;
        files.push_back(&entry);
        if (!seen)
            fileNames.push_back(entry.name);
    }

    std::map<std::string, std::uint64_t> existingFiles;
    if (parentOnMega && !fileNames.empty())
    {
        Result<std::vector<FileEntry>> hits =
            mClient->findChildFiles(parentHandle, parentIsRoot, fileNames);
        if (!hits.success)
            return Result<void>::fail(hits.errorMessage, hits.errorCode);
        existingFiles = handlesByName(hits.value());
    }
    // The batch's own second copy of a name lands on its first copy, so it
    // collides even where MEGA holds neither; the first one through is the
    // survivor, as on the copy/move path (FileMutationController::collidingEntries).
    std::set<std::string> arriving;
    for (const LocalEntry* local : files)
    {
        const bool broughtTwice = !arriving.insert(local->name).second;
        const auto hit = existingFiles.find(local->name);
        if (hit == existingFiles.end() && !broughtTwice)
            continue;
        out.collisions.push_back(UploadCollision{local->path,
                                                 local->name,
                                                 parentHandle,
                                                 parentIsRoot,
                                                 hit == existingFiles.end() ? 0 : hit->second});
    }

    if (directoryNames.empty())
        return Result<void>::ok();

    std::map<std::string, std::uint64_t> existingFolders;
    if (parentOnMega)
    {
        Result<std::vector<FileEntry>> folders =
            mClient->findChildFolders(parentHandle, parentIsRoot, directoryNames);
        if (!folders.success)
            return Result<void>::fail(folders.errorMessage, folders.errorCode);
        existingFolders = handlesByName(folders.value());
    }

    for (const std::string& name : directoryNames)
    {
        const std::vector<const LocalEntry*>& copies = directoryCopies[name];
        const auto hit = existingFolders.find(name);
        const bool onMega = hit != existingFolders.end();
        // One copy and nothing on MEGA to merge into: the SDK creates the folder
        // outright, so nothing inside it can land on anything.
        if (!onMega && copies.size() < 2)
            continue;
        // A truncated walk cannot tell "nothing collides" from "did not look", so
        // hitting the cycle stop fails the scan rather than answering short.
        if (depth + 1 >= kMaxDepth)
            return Result<void>::fail("Local folders are nested too deeply to check for name "
                                      "collisions",
                                      MegaErrorCode::kEInternal);
        std::vector<LocalEntry> level;
        for (const LocalEntry* copy : copies)
        {
            if (onMega)
                out.folderHandles.emplace(copy->path, hit->second);
            else
                out.createdFolders.insert(copy->path);
            // Unlike the skip plan, a folder that refuses to be listed only costs a
            // question here: finding no collisions under it leaves the plan handing it
            // to the SDK whole, so nothing is dropped by walking on.
            const std::optional<std::vector<LocalEntry>> children = mFs->listDirectory(copy->path);
            if (!children)
                continue;
            level.insert(level.end(), children->begin(), children->end());
        }
        if (level.empty())
            continue;
        // Below the top level the parent is always a real node, so parentIsRoot
        // stops applying once the walk descends.
        Result<void> deeper =
            scanLevel(level, onMega ? hit->second : 0, false, onMega, depth + 1, out);
        if (!deeper.success)
            return deeper;
    }
    return Result<void>::ok();
}
