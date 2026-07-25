#include "FolderNavigationService.h"

FolderNavigationService::FolderNavigationService(std::shared_ptr<IMegaClient> client)
    : mClient(std::move(client))
{
}

void FolderNavigationService::openFolder(
    std::uint64_t handle,
    std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    mClient->getChildren(handle, [this, handle, onDone](Result<std::vector<FileEntry>> result) {
        if (result.success)
        {
            mBackStack.push_back(mCurrent);
            mCurrent = Location{false, handle};
        }
        onDone(std::move(result));
    });
}

void FolderNavigationService::goBack(
    std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    if (mBackStack.empty())
    {
        onDone(Result<std::vector<FileEntry>>::fail("no back history"));
        return;
    }

    Location target = mBackStack.back();
    auto onFetched = [this, target, onDone](Result<std::vector<FileEntry>> result) {
        if (result.success)
        {
            mBackStack.pop_back();
            mCurrent = target;
        }
        onDone(std::move(result));
    };

    if (target.isRoot)
        mClient->getRootChildren(onFetched);
    else
        mClient->getChildren(target.handle, onFetched);
}

bool FolderNavigationService::canGoBack() const
{
    return !mBackStack.empty();
}

FolderNavigationService::CurrentLocation FolderNavigationService::currentLocation() const
{
    return CurrentLocation{mCurrent.isRoot, mCurrent.handle};
}
