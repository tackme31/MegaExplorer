#include "FolderNavigationService.h"

FolderNavigationService::FolderNavigationService(std::shared_ptr<IMegaClient> client)
    : mClient(std::move(client))
{}

void FolderNavigationService::openFolder(std::uint64_t handle,
                                         SortOrder order,
                                         std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    mClient->getChildren(
        handle, order, [this, handle, onDone](Result<std::vector<FileEntry>> result) {
            if (result.success)
            {
                mBackStack.push_back(mCurrent);
                mCurrent = Location{false, handle};
            }
            onDone(std::move(result));
        });
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
    auto onFetched = [this, target, onDone](Result<std::vector<FileEntry>> result) {
        if (result.success)
        {
            mBackStack.pop_back();
            mCurrent = target;
        }
        onDone(std::move(result));
    };

    if (target.isRoot)
        mClient->getRootChildren(order, onFetched);
    else
        mClient->getChildren(target.handle, order, onFetched);
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

FolderNavigationService::CurrentLocation FolderNavigationService::currentLocation() const
{
    return CurrentLocation{mCurrent.isRoot, mCurrent.handle};
}
