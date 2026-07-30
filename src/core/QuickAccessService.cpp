#include "QuickAccessService.h"

#include <algorithm>
#include <string>
#include <utility>

QuickAccessService::QuickAccessService(std::shared_ptr<IMegaClient> client,
                                       std::shared_ptr<IPinnedFolderStore> store)
    : mClient(std::move(client)), mStore(std::move(store))
{}

void QuickAccessService::load()
{
    Result<std::uint64_t> handle = mClient->currentUserHandle();
    if (!handle.success)
    {
        mAccountKey.clear();
        mPins.clear();
        return;
    }

    mAccountKey = std::to_string(handle.value);
    Result<std::vector<PinnedFolder>> stored = mStore->load(mAccountKey);
    mPins = stored.success ? std::move(stored.value) : std::vector<PinnedFolder>();
}

const std::vector<PinnedFolder>& QuickAccessService::pins() const
{
    return mPins;
}

bool QuickAccessService::isPinned(std::uint64_t handle) const
{
    return find(handle) != mPins.end();
}

bool QuickAccessService::pin(const PinnedFolder& folder)
{
    if (isPinned(folder.handle))
        return false;

    mPins.push_back(folder);
    if (!mAccountKey.empty())
        mStore->save(mAccountKey, mPins);
    return true;
}

bool QuickAccessService::unpin(std::uint64_t handle)
{
    std::vector<PinnedFolder>::const_iterator existing = find(handle);
    if (existing == mPins.end())
        return false;

    mPins.erase(existing);
    if (!mAccountKey.empty())
        mStore->save(mAccountKey, mPins);
    return true;
}

void QuickAccessService::replaceAll(std::vector<PinnedFolder> pins)
{
    mPins = std::move(pins);
    if (!mAccountKey.empty())
        mStore->save(mAccountKey, mPins);
}

void QuickAccessService::clear()
{
    mPins.clear();
    mAccountKey.clear();
}

void QuickAccessService::resolveFolder(std::uint64_t handle,
                                       std::function<void(Result<NodeInfo>)> onDone)
{
    mClient->getNodeInfo(handle, std::move(onDone));
}

bool QuickAccessService::isUsable(const Result<NodeInfo>& resolved)
{
    return resolved.success && resolved.value.isFolder && resolved.value.inCloud;
}

std::vector<PinnedFolder>::const_iterator QuickAccessService::find(std::uint64_t handle) const
{
    return std::find_if(mPins.begin(), mPins.end(), [handle](const PinnedFolder& pin) {
        return pin.handle == handle;
    });
}
