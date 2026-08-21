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
    // Stays on whichever screen it was opened from: the bin's contents are ordinary
    // nodes, so nothing but the kind distinguishes them from live ones, and that
    // kind is what gates every action here.
    navigateTo(handle,
               false,
               mCurrent.kind == ViewKind::Rubbish ? ViewKind::Rubbish : ViewKind::CloudDrive,
               order,
               std::move(onDone));
}

void FolderNavigationService::navigateTo(std::uint64_t handle,
                                         bool isRoot,
                                         ViewKind kind,
                                         SortOrder order,
                                         std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    runAndCommit(
        [this, handle, isRoot, kind, order](
            std::function<void(Result<std::vector<FileEntry>>)> onFetched) {
            if (isRoot && kind == ViewKind::Rubbish)
                mClient->getRubbishChildren(order, std::move(onFetched));
            else if (isRoot)
                mClient->getRootChildren(order, std::move(onFetched));
            else
                mClient->getChildren(handle, order, std::move(onFetched));
        },
        [this, handle, isRoot, kind] {
            mBackStack.push_back(mCurrent);
            mCurrent = Location{kind, isRoot, handle};
        },
        std::move(onDone));
}

void FolderNavigationService::openFavourites(
    SortOrder order, std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    if (mCurrent.kind == ViewKind::Favourites)
    {
        mClient->listFavourites(order, "", SearchFilter{}, std::move(onDone));
        return;
    }

    runAndCommit(
        [this, order](std::function<void(Result<std::vector<FileEntry>>)> onFetched) {
            mClient->listFavourites(order, "", SearchFilter{}, std::move(onFetched));
        },
        [this] {
            mBackStack.push_back(mCurrent);
            mCurrent = Location{ViewKind::Favourites, false, 0};
        },
        std::move(onDone));
}

void FolderNavigationService::openRubbish(
    SortOrder order, std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    // Only the bin's own top counts as "already there": a folder inside it is a
    // different location, so clicking the side-panel row from there navigates.
    if (mCurrent.kind == ViewKind::Rubbish && mCurrent.isRoot)
    {
        mClient->getRubbishChildren(order, std::move(onDone));
        return;
    }

    runAndCommit(
        [this, order](std::function<void(Result<std::vector<FileEntry>>)> onFetched) {
            mClient->getRubbishChildren(order, std::move(onFetched));
        },
        [this] {
            mBackStack.push_back(mCurrent);
            mCurrent = Location{ViewKind::Rubbish, true, 0};
        },
        std::move(onDone));
}

void FolderNavigationService::openRecents(
    SortOrder order, std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    if (mCurrent.kind == ViewKind::Recents)
    {
        mClient->listRecent(order, "", SearchFilter{}, std::move(onDone));
        return;
    }

    runAndCommit(
        [this, order](std::function<void(Result<std::vector<FileEntry>>)> onFetched) {
            mClient->listRecent(order, "", SearchFilter{}, std::move(onFetched));
        },
        [this] {
            mBackStack.push_back(mCurrent);
            mCurrent = Location{ViewKind::Recents, false, 0};
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
            fetchListing(target, order, std::move(onFetched));
        },
        [this, target] {
            mBackStack.pop_back();
            mCurrent = target;
        },
        std::move(onDone));
}

void FolderNavigationService::fetchListing(
    const Location& location,
    SortOrder order,
    std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    if (location.kind == ViewKind::Favourites)
        mClient->listFavourites(order, "", SearchFilter{}, std::move(onDone));
    else if (location.kind == ViewKind::Recents)
        mClient->listRecent(order, "", SearchFilter{}, std::move(onDone));
    else if (location.kind == ViewKind::Rubbish && location.isRoot)
        mClient->getRubbishChildren(order, std::move(onDone));
    else if (location.isRoot)
        mClient->getRootChildren(order, std::move(onDone));
    else
        mClient->getChildren(location.handle, order, std::move(onDone));
}

void FolderNavigationService::refreshCurrent(
    SortOrder order, std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    fetchListing(mCurrent, order, std::move(onDone));
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
    const SearchFilter& filter,
    std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    mClient->listFavourites(order, nameFilter, filter, std::move(onDone));
}

void FolderNavigationService::listRecent(
    SortOrder order,
    const std::string& nameFilter,
    const SearchFilter& filter,
    std::function<void(Result<std::vector<FileEntry>>)> onDone)
{
    mClient->listRecent(order, nameFilter, filter, std::move(onDone));
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
    // Neither flat listing has an ancestor chain to resolve: each is one synthesized
    // segment, deliberately nameless so QML owns the label like it does the root's
    // (FAVOURITES_VIEW_SPEC.md 3.3, amended -- src/core links no Qt, so a literal
    // here could never be translated).
    if (mCurrent.kind == ViewKind::Favourites || mCurrent.kind == ViewKind::Recents)
    {
        onDone(
            Result<std::vector<PathSegment>>::ok({PathSegment{"", 0, false, mCurrent.kind}}));
        return;
    }
    // The bin's own top is synthesized for the same reason, and marked isRoot so
    // QML can tell it from a folder inside the bin -- which does have an ancestor
    // chain and resolves normally below.
    if (mCurrent.kind == ViewKind::Rubbish && mCurrent.isRoot)
    {
        onDone(
            Result<std::vector<PathSegment>>::ok({PathSegment{"", 0, true, ViewKind::Rubbish}}));
        return;
    }
    mClient->getPath(mCurrent.handle, mCurrent.isRoot, std::move(onDone));
}

void FolderNavigationService::resolvePathOf(
    std::uint64_t handle, std::function<void(Result<std::vector<PathSegment>>)> onDone)
{
    // isRoot false unconditionally: a caller here names a node it saw in a listing,
    // and no listing contains a root.
    mClient->getPath(handle, false, std::move(onDone));
}

void FolderNavigationService::syncWithServer(std::function<void(Result<void>)> onDone)
{
    mClient->syncPendingChanges(std::move(onDone));
}
