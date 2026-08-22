#include "ThumbnailService.h"

namespace
{

// Same struct, same reasons, as DownloadService.cpp's -- see there, including why the
// ordinary exits clear the flag themselves instead of leaving it to the destructor.
struct AdvancingGuard
{
    std::mutex& mutex;
    bool& flag;
    bool armed = true;

    // mutex must be held.
    void clearHeld()
    {
        flag = false;
        armed = false;
    }

    ~AdvancingGuard()
    {
        if (!armed)
            return;
        std::lock_guard<std::mutex> lock(mutex);
        flag = false;
    }
};

} // namespace

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
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mAdvancing)
        {
            mAdvanceRequested = true; // whoever is in the loop below picks it up
            return;
        }
        mAdvancing = true;
    }
    AdvancingGuard advancing = {mMutex, mAdvancing};

    for (;;)
    {
        std::uint64_t handle = 0;
        std::string destinationPath;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mAdvanceRequested = false;
            if (mActiveCount >= mMaxConcurrent || mQueue.empty())
            {
                advancing.clearHeld();
                return;
            }
            handle = mQueue.front();
            mQueue.pop_front();
            Job& job = mJobs.at(handle);
            destinationPath = job.destinationPath;
            job.started = true;
            ++mActiveCount;
        }

        try
        {
            mClient->getThumbnail(
                handle, destinationPath, [this, handle](Result<std::string> result) {
                    finishJob(handle, result);
                });
        }
        catch (...)
        {
            // Give the slot back only if this very entry is still the one that was
            // started: a completion that ran in-stack has already released it, and a
            // re-request from its callbacks re-creates an unstarted entry under the same
            // handle -- decrementing for that one wraps mActiveCount and wedges the queue.
            std::lock_guard<std::mutex> lock(mMutex);
            auto jobIt = mJobs.find(handle);
            if (jobIt != mJobs.end() && jobIt->second.started)
            {
                mJobs.erase(jobIt); // its callbacks are dropped; nothing will ever call them
                --mActiveCount;
            }
            throw;
        }

        // A synchronous failure has already run the whole finishJob above by
        // now, and its startNextIfCapacity() only set the flag -- so keep
        // looping here instead of letting it recurse. A genuinely in-flight
        // request leaves the flag clear and this call ends. Both branches must
        // share one lock: splitting them lets a completion land in between,
        // set the flag, and find nobody left to act on it.
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mAdvanceRequested)
        {
            advancing.clearHeld();
            return;
        }
    }
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
            mCache[handle] = result.value();
        --mActiveCount;
    }
    for (auto& cb : callbacks)
        cb(result);
    startNextIfCapacity(); // auto-advance; mMutex isn't held here
}
