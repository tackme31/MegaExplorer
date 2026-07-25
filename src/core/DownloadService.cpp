#include "DownloadService.h"

DownloadService::DownloadService(std::shared_ptr<IMegaClient> client) : mClient(std::move(client))
{}

std::uint64_t DownloadService::enqueue(std::uint64_t handle,
                                       const std::string& name,
                                       const std::string& destinationPath,
                                       std::uint64_t expectedTotalBytes)
{
    std::uint64_t id;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        DownloadJob job;
        job.id = mNextId++;
        job.handle = handle;
        job.name = name;
        job.destinationPath = destinationPath;
        job.totalBytes = expectedTotalBytes;
        mQueue.push_back(job);
        id = job.id;
    }
    startNextIfIdle();
    return id;
}

bool DownloadService::hasCurrentJob() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return !mQueue.empty();
}

DownloadJob DownloadService::currentJob() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mQueue.front();
}

std::vector<DownloadJob> DownloadService::jobs() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mQueue;
}

void DownloadService::setOnProgress(std::function<void(DownloadJob)> onProgress)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mOnProgress = std::move(onProgress);
}

void DownloadService::setOnJobFinished(std::function<void(DownloadJob)> onJobFinished)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mOnJobFinished = std::move(onJobFinished);
}

void DownloadService::startNextIfIdle()
{
    std::uint64_t handle;
    std::string destinationPath;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mQueue.empty() || mQueue.front().state != DownloadState::Queued)
            return;
        mQueue.front().state = DownloadState::Active;
        handle = mQueue.front().handle;
        destinationPath = mQueue.front().destinationPath;
    }

    mClient->download(
        handle,
        destinationPath,
        [this](std::uint64_t transferred, std::uint64_t total) {
            std::function<void(DownloadJob)> onProgress;
            DownloadJob snapshot;
            {
                std::lock_guard<std::mutex> lock(mMutex);
                if (mQueue.empty())
                    return;
                mQueue.front().transferredBytes = transferred;
                mQueue.front().totalBytes = total;
                snapshot = mQueue.front();
                onProgress = mOnProgress;
            }
            if (onProgress)
                onProgress(snapshot);
        },
        [this](Result<std::string> result) {
            std::function<void(DownloadJob)> onJobFinished;
            DownloadJob snapshot;
            {
                std::lock_guard<std::mutex> lock(mMutex);
                if (mQueue.empty())
                    return;
                mQueue.front().state =
                    result.success ? DownloadState::Completed : DownloadState::Failed;
                if (result.success)
                    mQueue.front().resolvedLocalPath = result.value;
                else
                    mQueue.front().errorMessage = result.errorMessage;
                snapshot = mQueue.front();
                onJobFinished = mOnJobFinished;
                mQueue.erase(mQueue.begin());
            }
            if (onJobFinished)
                onJobFinished(snapshot);
            startNextIfIdle(); // auto-advance; mMutex isn't held here
        });
}
