#include "FolderNavigationService.h"

FolderNavigationService::FolderNavigationService(std::shared_ptr<IMegaClient> client,
                                                 std::shared_ptr<INodeCache> cache)
    : mClient(std::move(client)), mCache(std::move(cache))
{}

void FolderNavigationService::loadWithCache(
    const INodeCache::ParentKey& key,
    std::function<void(std::function<void(Result<std::vector<FileEntry>>)>)> network,
    std::function<void()> onCommit,
    std::function<void(std::vector<FileEntry>)> onCacheHit,
    std::function<void(Result<std::vector<FileEntry>>)> onRefreshed)
{
    Result<std::vector<FileEntry>> cached = mCache->loadChildren(key);
    if (cached.success && !cached.value.empty())
        onCacheHit(cached.value);

    network([this, key, onCommit, onRefreshed](Result<std::vector<FileEntry>> result) {
        if (result.success)
        {
            onCommit();
            // Write-through failures are silently swallowed here: src/core
            // stays Qt-free (no logging facility), and SqliteNodeCache
            // already logs its own failures via lcCache before returning
            // Result::fail -- nothing useful to add at this layer, and a
            // cache miss just means the next open falls back to
            // network-only, never a user-visible error.
            (void)mCache->saveChildren(key, result.value);
        }
        onRefreshed(std::move(result));
    });
}

void FolderNavigationService::openRoot(
    SortOrder order,
    std::function<void(std::vector<FileEntry>)> onCacheHit,
    std::function<void(Result<std::vector<FileEntry>>)> onRefreshed)
{
    loadWithCache(
        INodeCache::ParentKey{true, 0},
        [this, order](std::function<void(Result<std::vector<FileEntry>>)> onFetched) {
            mClient->getRootChildren(order, std::move(onFetched));
        },
        [] {}, // root is never pushed/popped on the back-stack
        std::move(onCacheHit),
        std::move(onRefreshed));
}

void FolderNavigationService::openFolder(
    std::uint64_t handle,
    SortOrder order,
    std::function<void(std::vector<FileEntry>)> onCacheHit,
    std::function<void(Result<std::vector<FileEntry>>)> onRefreshed)
{
    loadWithCache(
        INodeCache::ParentKey{false, handle},
        [this, handle, order](std::function<void(Result<std::vector<FileEntry>>)> onFetched) {
            mClient->getChildren(handle, order, std::move(onFetched));
        },
        [this, handle] {
            mBackStack.push_back(mCurrent);
            mCurrent = Location{false, handle};
        },
        std::move(onCacheHit),
        std::move(onRefreshed));
}

void FolderNavigationService::goBack(
    SortOrder order,
    std::function<void(std::vector<FileEntry>)> onCacheHit,
    std::function<void(Result<std::vector<FileEntry>>)> onRefreshed)
{
    if (mBackStack.empty())
    {
        onRefreshed(Result<std::vector<FileEntry>>::fail("no back history"));
        return;
    }

    Location target = mBackStack.back();
    loadWithCache(
        INodeCache::ParentKey{target.isRoot, target.handle},
        [this, target, order](std::function<void(Result<std::vector<FileEntry>>)> onFetched) {
            if (target.isRoot)
                mClient->getRootChildren(order, std::move(onFetched));
            else
                mClient->getChildren(target.handle, order, std::move(onFetched));
        },
        [this, target] {
            mBackStack.pop_back();
            mCurrent = target;
        },
        std::move(onCacheHit),
        std::move(onRefreshed));
}

void FolderNavigationService::refreshCurrent(
    SortOrder order, std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    const INodeCache::ParentKey key{mCurrent.isRoot, mCurrent.handle};
    auto onFetched = [this, key, onDone](Result<std::vector<FileEntry>> result) {
        if (result.success)
        {
            // See loadWithCache's comment: write-through failures are
            // silently swallowed at this layer by design.
            (void)mCache->saveChildren(key, result.value);
        }
        onDone(std::move(result));
    };

    if (mCurrent.isRoot)
        mClient->getRootChildren(order, std::move(onFetched));
    else
        mClient->getChildren(mCurrent.handle, order, std::move(onFetched));
}

bool FolderNavigationService::canGoBack() const
{
    return !mBackStack.empty();
}

void FolderNavigationService::resetToRoot()
{
    mBackStack.clear();
    mCurrent = Location{};
}

FolderNavigationService::CurrentLocation FolderNavigationService::currentLocation() const
{
    return CurrentLocation{mCurrent.isRoot, mCurrent.handle};
}
