#include "FolderNavigationService.h"

FolderNavigationService::FolderNavigationService(std::shared_ptr<IMegaClient> client)
    : mClient(std::move(client))
{}

void FolderNavigationService::runAndCommit(
    std::function<void(std::function<void(Result<std::vector<FileEntry>>)>)> network,
    std::function<void()> onCommit,
    std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    network([onCommit, onDone](Result<std::vector<FileEntry>> result) {
        if (result.success)
            onCommit();
        onDone(std::move(result));
    });
}

void FolderNavigationService::openRoot(SortOrder order,
                                       std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    runAndCommit(
        [this, order](std::function<void(Result<std::vector<FileEntry>>)> onFetched) {
            mClient->getRootChildren(order, std::move(onFetched));
        },
        [] {}, // root is never pushed/popped on the back-stack
        std::move(onDone));
}

void FolderNavigationService::openFolder(std::uint64_t handle,
                                         SortOrder order,
                                         std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    navigateTo(handle, false, order, std::move(onDone));
}

void FolderNavigationService::navigateTo(std::uint64_t handle,
                                         bool isRoot,
                                         SortOrder order,
                                         std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    runAndCommit(
        [this, handle, isRoot, order](
            std::function<void(Result<std::vector<FileEntry>>)> onFetched) {
            if (isRoot)
                mClient->getRootChildren(order, std::move(onFetched));
            else
                mClient->getChildren(handle, order, std::move(onFetched));
        },
        [this, handle, isRoot] {
            mBackStack.push_back(mCurrent);
            mCurrent = Location{isRoot, handle};
        },
        std::move(onDone));
}

void FolderNavigationService::goBack(SortOrder order,
                                     std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    if (mBackStack.empty())
    {
        onDone(Result<std::vector<FileEntry>>::fail("no back history"));
        return;
    }

    Location target = mBackStack.back();
    runAndCommit(
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
        std::move(onDone));
}

void FolderNavigationService::refreshCurrent(
    SortOrder order, std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    if (mCurrent.isRoot)
        mClient->getRootChildren(order, std::move(onDone));
    else
        mClient->getChildren(mCurrent.handle, order, std::move(onDone));
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

void FolderNavigationService::resolveCurrentPath(
    std::function<void(Result<std::vector<PathSegment>>)> onDone)
{
    mClient->getPath(mCurrent.handle, mCurrent.isRoot, std::move(onDone));
}

void FolderNavigationService::syncWithServer(std::function<void(Result<void>)> onDone)
{
    mClient->syncPendingChanges(std::move(onDone));
}
