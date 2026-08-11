#include "FolderNavigationService.h"

#include "MegaErrorCodes.h"

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
            mCurrent = Location{ViewKind::CloudDrive, isRoot, handle};
        },
        std::move(onDone));
}

void FolderNavigationService::openFavourites(
    SortOrder order, std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    if (mCurrent.kind == ViewKind::Favourites)
    {
        mClient->listFavourites(order, "", std::move(onDone));
        return;
    }

    runAndCommit(
        [this, order](std::function<void(Result<std::vector<FileEntry>>)> onFetched) {
            mClient->listFavourites(order, "", std::move(onFetched));
        },
        [this] {
            mBackStack.push_back(mCurrent);
            mCurrent = Location{ViewKind::Favourites, false, 0};
        },
        std::move(onDone));
}

void FolderNavigationService::goBack(SortOrder order,
                                     std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    if (mBackStack.empty())
    {
        onDone(Result<std::vector<FileEntry>>::fail("no back history", MegaErrorCode::kEArgs));
        return;
    }

    Location target = mBackStack.back();
    runAndCommit(
        [this, target, order](std::function<void(Result<std::vector<FileEntry>>)> onFetched) {
            if (target.kind == ViewKind::Favourites)
                mClient->listFavourites(order, "", std::move(onFetched));
            else if (target.isRoot)
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
    if (mCurrent.kind == ViewKind::Favourites)
        mClient->listFavourites(order, "", std::move(onDone));
    else if (mCurrent.isRoot)
        mClient->getRootChildren(order, std::move(onDone));
    else
        mClient->getChildren(mCurrent.handle, order, std::move(onDone));
}

void FolderNavigationService::listChildrenOf(
    std::uint64_t handle,
    bool isRoot,
    SortOrder order,
    std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    if (isRoot)
        mClient->getRootChildren(order, std::move(onDone));
    else
        mClient->getChildren(handle, order, std::move(onDone));
}

void FolderNavigationService::listFavourites(
    SortOrder order,
    const std::string& nameFilter,
    std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    mClient->listFavourites(order, nameFilter, std::move(onDone));
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
    return CurrentLocation{mCurrent.kind, mCurrent.isRoot, mCurrent.handle};
}

void FolderNavigationService::resolveCurrentPath(
    std::function<void(Result<std::vector<PathSegment>>)> onDone)
{
    // The favourites listing has no ancestor chain to resolve: it is one synthesized
    // segment, deliberately nameless so QML owns the label like it does the root's
    // (FAVOURITES_VIEW_SPEC.md 3.3, amended -- src/core links no Qt, so a literal
    // here could never be translated).
    if (mCurrent.kind == ViewKind::Favourites)
    {
        onDone(Result<std::vector<PathSegment>>::ok(
            {PathSegment{"", 0, false, ViewKind::Favourites}}));
        return;
    }
    mClient->getPath(mCurrent.handle, mCurrent.isRoot, std::move(onDone));
}

void FolderNavigationService::syncWithServer(std::function<void(Result<void>)> onDone)
{
    mClient->syncPendingChanges(std::move(onDone));
}
