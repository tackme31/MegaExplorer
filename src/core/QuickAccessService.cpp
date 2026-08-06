#include "QuickAccessService.h"

#include <algorithm>
#include <string>
#include <utility>

QuickAccessService::QuickAccessService(std::shared_ptr<IMegaClient> client,
                                       std::shared_ptr<IPinnedFolderStore> store)
    : mClient(std::move(client)), mStore(std::move(store))
{}

Result<void> QuickAccessService::load()
{
    Result<std::uint64_t> handle = mClient->currentUserHandle();
    if (!handle.success)
    {
        mAccountKey.clear();
        mPins.clear();
        return Result<void>::fail(std::move(handle.errorMessage), handle.errorCode);
    }

    mAccountKey = std::to_string(handle.value);
    Result<std::vector<PinnedFolder>> stored = mStore->load(mAccountKey);
    mPins = stored.success ? std::move(stored.value) : std::vector<PinnedFolder>();
    if (!stored.success)
        return Result<void>::fail(std::move(stored.errorMessage), stored.errorCode);
    return Result<void>::ok();
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
    persist();
    return true;
}

bool QuickAccessService::unpin(std::uint64_t handle)
{
    std::vector<PinnedFolder>::const_iterator existing = find(handle);
    if (existing == mPins.end())
        return false;

    mPins.erase(existing);
    persist();
    return true;
}

bool QuickAccessService::move(std::size_t from, std::size_t to)
{
    if (from >= mPins.size() || to >= mPins.size() || from == to)
        return false;

    const auto first = mPins.begin();
    const auto at = [first](std::size_t i) {
        return first + static_cast<std::vector<PinnedFolder>::difference_type>(i);
    };

    if (from < to)
        std::rotate(at(from), at(from + 1), at(to + 1));
    else
        std::rotate(at(to), at(from), at(from + 1));

    persist();
    return true;
}

void QuickAccessService::replaceAll(std::vector<PinnedFolder> pins)
{
    mPins = std::move(pins);
    persist();
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

void QuickAccessService::setOnPersistenceFailed(std::function<void(const Result<void>&)> handler)
{
    mOnPersistenceFailed = std::move(handler);
}

void QuickAccessService::persist()
{
    // No account resolved means there's no storage slot to write to (Phase
    // 11a), not a failed write -- load() has already reported that.
    if (mAccountKey.empty())
        return;

    const Result<void> saved = mStore->save(mAccountKey, mPins);
    if (!saved.success && mOnPersistenceFailed)
        mOnPersistenceFailed(saved);
}

std::vector<PinnedFolder>::const_iterator QuickAccessService::find(std::uint64_t handle) const
{
    return std::find_if(mPins.begin(), mPins.end(), [handle](const PinnedFolder& pin) {
        return pin.handle == handle;
    });
}
