#include "ThumbnailService.h"

namespace
{

// The path crosses into the SDK's LocalPath, which splits on '\' on Windows -- a '/'
// in the middle would be read as part of a name instead of as a directory boundary.
#ifdef _WIN32
constexpr char kPathSeparator = '\\';
#else
constexpr char kPathSeparator = '/';
#endif

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

ThumbnailService::ThumbnailService(std::shared_ptr<IMegaClient> client,
                                  std::shared_ptr<ILocalFileSystem> fileSystem,
                                  std::string cacheDirectory,
                                  std::size_t maxConcurrent)
    : mClient(std::move(client)), mFileSystem(std::move(fileSystem)),
      mCacheDirectory(std::move(cacheDirectory)), mMaxConcurrent(maxConcurrent)
{}

void ThumbnailService::request(std::uint64_t handle,
                               std::function<void(Result<std::string>)> onDone)
{
    // Asked on every request, not resolved once: signing out and into another account
    // does not restart the process, and the directory is what keeps the two apart.
    const Result<std::uint64_t> user = mClient->currentUserHandle();
    if (!user.success)
    {
        onDone(Result<std::string>::fail(user.errorMessage, user.errorCode));
        return;
    }
    const std::string directory =
        mCacheDirectory + kPathSeparator + std::to_string(user.value());
    const std::string path = directory + kPathSeparator + std::to_string(handle) + ".jpg";

    bool cacheHit = false;
    std::string cachedPath;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (directory != mAccountDirectory)
        {
            mCache.clear(); // every path in it names the account just signed out of
            mAccountDirectory = directory;
        }
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
            return;
        }
    }
    if (cacheHit)
    {
        onDone(Result<std::string>::ok(cachedPath));
        return;
    }

    // Disk calls, so no lock is held for them. An empty file is a fetch that died
    // part-way: treating it as a hit would pin a broken image forever, since nothing
    // ever revisits a handle the cache already answers.
    const std::optional<LocalEntry> onDisk = mFileSystem->entryFor(path);
    const bool diskHit = onDisk.has_value() && !onDisk->isDirectory && onDisk->sizeBytes > 0;
    if (!diskHit)
        mFileSystem->createDirectory(directory);

    bool isNewJob = false;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        // Looked up again because the lock was dropped for the two calls above.
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
            return;
        }
        else if (diskHit)
        {
            mCache[handle] = path;
            cacheHit = true;
            cachedPath = path;
        }
        else
        {
            Job job;
            job.destinationPath = path;
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
