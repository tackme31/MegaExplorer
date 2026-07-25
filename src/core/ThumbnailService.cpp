#include "ThumbnailService.h"

ThumbnailService::ThumbnailService(std::shared_ptr<IMegaClient> client, std::size_t maxConcurrent)
    : mClient(std::move(client)), mMaxConcurrent(maxConcurrent)
{}

void ThumbnailService::request(std::uint64_t handle,
                               const std::string& destinationPath,
                               std::function<void(Result<std::string>)> onDone)
{
    bool cacheHit = false;
    std::string cachedPath;
    bool isNewJob = false;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto cacheIt = mCache.find(handle);
        auto jobIt = mJobs.find(handle);
        if (cacheIt != mCache.end())
        {
            cacheHit = true;
            cachedPath = cacheIt->second;
        }
        else if (jobIt != mJobs.end())
        {
            jobIt->second.callbacks.push_back(std::move(onDone));
        }
        else
        {
            Job job;
            job.destinationPath = destinationPath;
            job.callbacks.push_back(std::move(onDone));
            mJobs.emplace(handle, std::move(job));
            mQueue.push_back(handle);
            isNewJob = true;
        }
    }

    if (cacheHit)
        onDone(Result<std::string>::ok(cachedPath));
    else if (isNewJob)
        startNextIfCapacity();
}

void ThumbnailService::startNextIfCapacity()
{
    std::uint64_t handle = 0;
    std::string destinationPath;
    bool started = false;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mActiveCount < mMaxConcurrent && !mQueue.empty())
        {
            handle = mQueue.front();
            mQueue.pop_front();
            destinationPath = mJobs.at(handle).destinationPath;
            ++mActiveCount;
            started = true;
        }
    }
    if (!started)
        return;

    mClient->getThumbnail(handle, destinationPath, [this, handle](Result<std::string> result) {
        finishJob(handle, result);
    });
}

void ThumbnailService::finishJob(std::uint64_t handle, Result<std::string> result)
{
    std::vector<std::function<void(Result<std::string>)>> callbacks;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        auto jobIt = mJobs.find(handle);
        if (jobIt == mJobs.end())
            return;
        callbacks = std::move(jobIt->second.callbacks);
        mJobs.erase(jobIt);
        if (result.success)
            mCache[handle] = result.value;
        --mActiveCount;
    }
    for (auto& cb : callbacks)
        cb(result);
    startNextIfCapacity(); // auto-advance; mMutex isn't held here
}
