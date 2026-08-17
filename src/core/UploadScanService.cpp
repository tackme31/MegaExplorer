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
        addToPlan(entry, parentHandle, parentIsRoot, scanned.value(), collided, plan);
    return Result<std::vector<UploadPlanItem>>::ok(std::move(plan));
}

void UploadScanService::addToPlan(const LocalEntry& entry,
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
        return;
    }

    // Only a directory the walk descended into can hold a collision, and only one
    // that actually holds a live one is worth taking apart -- everything else goes
    // up as a single SDK folder transfer.
    const auto merged = scanned.folderHandles.find(entry.path);
    if (merged == scanned.folderHandles.end() || !hasCollisionUnder(collided, entry.path))
    {
        plan.push_back(UploadPlanItem{entry.path, parentHandle, parentIsRoot});
        return;
    }
    for (const LocalEntry& child : mFs->listDirectory(entry.path))
        addToPlan(child, merged->second, false, scanned, collided, plan);
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
    Result<void> walk = scanLevel(topLevel, parentHandle, parentIsRoot, 0, scanned);
    if (!walk.success)
        return Result<Scan>::fail(walk.errorMessage, walk.errorCode);
    scanned.topLevel = std::move(topLevel);
    return Result<Scan>::ok(std::move(scanned));
}

Result<void> UploadScanService::scanLevel(const std::vector<LocalEntry>& entries,
                                          std::uint64_t parentHandle,
                                          bool parentIsRoot,
                                          int depth,
                                          Scan& out) const
{
    // Two of the paths handed here can share a leaf name (dragged from different
    // folders), so each name is asked about once and the answer is paired back to
    // *every* path carrying it. Dropping the twin instead would leave its path out
    // of the skip plan, which keys on path -- and it would then upload over the
    // node the user just chose to spare.
    std::vector<const LocalEntry*> files;
    std::vector<const LocalEntry*> directories;
    std::vector<std::string> fileNames;
    std::vector<std::string> directoryNames;
    for (const LocalEntry& entry : entries)
    {
        std::vector<const LocalEntry*>& sameKind = entry.isDirectory ? directories : files;
        std::vector<std::string>& names = entry.isDirectory ? directoryNames : fileNames;
        bool seen = false;
        for (const LocalEntry* known : sameKind)
            seen = seen || known->name == entry.name;
        sameKind.push_back(&entry);
        if (!seen)
            names.push_back(entry.name);
    }

    if (!fileNames.empty())
    {
        Result<std::vector<FileEntry>> hits =
            mClient->findChildFiles(parentHandle, parentIsRoot, fileNames);
        if (!hits.success)
            return Result<void>::fail(hits.errorMessage, hits.errorCode);
        const std::map<std::string, std::uint64_t> existing = handlesByName(hits.value());
        // The batch's own second copy of a name lands on its first copy, so it
        // collides even where MEGA holds neither; the first one through is the
        // survivor, as on the copy/move path (FileMutationController::collidingEntries).
        std::set<std::string> arriving;
        for (const LocalEntry* local : files)
        {
            const bool broughtTwice = !arriving.insert(local->name).second;
            const auto hit = existing.find(local->name);
            if (hit == existing.end() && !broughtTwice)
                continue;
            out.collisions.push_back(UploadCollision{local->path,
                                                     local->name,
                                                     parentHandle,
                                                     parentIsRoot,
                                                     hit == existing.end() ? 0 : hit->second});
        }
    }

    if (directoryNames.empty())
        return Result<void>::ok();

    Result<std::vector<FileEntry>> folders =
        mClient->findChildFolders(parentHandle, parentIsRoot, directoryNames);
    if (!folders.success)
        return Result<void>::fail(folders.errorMessage, folders.errorCode);
    const std::map<std::string, std::uint64_t> existing = handlesByName(folders.value());
    for (const LocalEntry* local : directories)
    {
        const auto hit = existing.find(local->name);
        if (hit == existing.end())
            continue;
        // A truncated walk cannot tell "nothing collides" from "did not look", so
        // hitting the cycle stop fails the scan rather than answering short.
        if (depth + 1 >= kMaxDepth)
            return Result<void>::fail("Local folders are nested too deeply to check for name "
                                      "collisions",
                                      MegaErrorCode::kEInternal);
        out.folderHandles.emplace(local->path, hit->second);
        // Below the top level the parent is always a real node, so parentIsRoot
        // stops applying once the walk descends.
        Result<void> deeper =
            scanLevel(mFs->listDirectory(local->path), hit->second, false, depth + 1, out);
        if (!deeper.success)
            return deeper;
    }
    return Result<void>::ok();
}
