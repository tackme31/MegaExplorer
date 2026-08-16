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
} // namespace

UploadScanService::UploadScanService(std::shared_ptr<IMegaClient> client,
                                     std::shared_ptr<ILocalFileSystem> fs)
    : mClient(std::move(client)), mFs(std::move(fs))
{}

Result<std::vector<UploadCollision>> UploadScanService::findCollisions(
    const std::vector<std::string>& localPaths, std::uint64_t parentHandle, bool parentIsRoot) const
{
    std::vector<LocalEntry> topLevel;
    topLevel.reserve(localPaths.size());
    for (const std::string& path : localPaths)
    {
        if (std::optional<LocalEntry> entry = mFs->entryFor(path))
            topLevel.push_back(*entry);
    }

    std::vector<UploadCollision> collisions;
    Result<void> scan = scanLevel(topLevel, parentHandle, parentIsRoot, 0, collisions);
    if (!scan.success)
        return Result<std::vector<UploadCollision>>::fail(scan.errorMessage, scan.errorCode);
    return Result<std::vector<UploadCollision>>::ok(std::move(collisions));
}

Result<void> UploadScanService::scanLevel(const std::vector<LocalEntry>& entries,
                                          std::uint64_t parentHandle,
                                          bool parentIsRoot,
                                          int depth,
                                          std::vector<UploadCollision>& out) const
{
    // Two of the paths handed to findCollisions can share a leaf name (dragged
    // from different folders), so each name is asked about once and answers are
    // paired back by name. Which of the two local files then owns the collision
    // row is left to the same-name-within-one-operation item, not decided here.
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
        if (seen)
            continue;
        sameKind.push_back(&entry);
        names.push_back(entry.name);
    }

    if (!fileNames.empty())
    {
        Result<std::vector<FileEntry>> hits =
            mClient->findChildFiles(parentHandle, parentIsRoot, fileNames);
        if (!hits.success)
            return Result<void>::fail(hits.errorMessage, hits.errorCode);
        const std::map<std::string, std::uint64_t> existing = handlesByName(hits.value());
        for (const LocalEntry* local : files)
        {
            const auto hit = existing.find(local->name);
            if (hit == existing.end())
                continue;
            out.push_back(
                UploadCollision{local->path, local->name, parentHandle, parentIsRoot, hit->second});
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
        // Below the top level the parent is always a real node, so parentIsRoot
        // stops applying once the walk descends.
        Result<void> deeper =
            scanLevel(mFs->listDirectory(local->path), hit->second, false, depth + 1, out);
        if (!deeper.success)
            return deeper;
    }
    return Result<void>::ok();
}
