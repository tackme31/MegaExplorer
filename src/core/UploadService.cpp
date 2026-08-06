#include "UploadService.h"

UploadService::UploadService(std::shared_ptr<IMegaClient> client) : mClient(std::move(client)) {}

std::uint64_t UploadService::enqueue(const std::string& localPath,
                                     const std::string& name,
                                     std::uint64_t parentHandle,
                                     bool parentIsRoot,
                                     std::uint64_t expectedTotalBytes,
                                     std::uint64_t replaceHandle)
{
    std::uint64_t id;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        UploadJob job;
        job.id = mNextId++;
        job.localPath = localPath;
        job.name = name;
        job.parentHandle = parentHandle;
        job.parentIsRoot = parentIsRoot;
        job.replaceHandle = replaceHandle;
        job.totalBytes = expectedTotalBytes;
        mQueue.push_back(job);
        id = job.id;
    }
    startNextIfIdle();
    return id;
}

std::optional<UploadJob> UploadService::currentJob() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mQueue.empty())
        return std::nullopt;
    return mQueue.front();
}

std::vector<UploadJob> UploadService::jobs() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mQueue;
}

std::size_t UploadService::queueLength() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mQueue.size();
}

Result<void> UploadService::canUploadTo(std::uint64_t parentHandle, bool parentIsRoot) const
{
    return mClient->checkUpload(parentHandle, parentIsRoot);
}

Result<std::vector<FileEntry>> UploadService::findNameCollisions(
    std::uint64_t parentHandle, bool parentIsRoot, const std::vector<std::string>& names) const
{
    return mClient->findChildFiles(parentHandle, parentIsRoot, names);
}

void UploadService::setOnProgress(std::function<void(UploadJob)> onProgress)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mOnProgress = std::move(onProgress);
}

void UploadService::setOnJobFinished(std::function<void(UploadJob)> onJobFinished)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mOnJobFinished = std::move(onJobFinished);
}

void UploadService::startNextIfIdle()
{
    // Two synchronous failure paths feed this loop, both reachable in
    // production (drop 200 files onto a folder that has just been deleted
    // elsewhere): the destination re-validation below, and IMegaClient::upload
    // itself when the parent handle no longer resolves. The first returns here
    // directly; the second comes back through onDone's startNextIfIdle(),
    // which the mAdvancing trampoline turns into another turn of this loop
    // rather than a nested frame. Same shape in DownloadService and
    // ThumbnailService.
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mAdvancing)
        {
            mAdvanceRequested = true;
            return;
        }
        mAdvancing = true;
    }

    for (;;)
    {
        std::string localPath;
        std::uint64_t parentHandle;
        bool parentIsRoot;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mAdvanceRequested = false;
            if (mQueue.empty() || mQueue.front().state != UploadState::Queued)
            {
                mAdvancing = false;
                return;
            }
            mQueue.front().state = UploadState::Active;
            localPath = mQueue.front().localPath;
            parentHandle = mQueue.front().parentHandle;
            parentIsRoot = mQueue.front().parentIsRoot;
        }

        // The destination may have been deleted between the drop and this
        // job's turn -- the hover-time checkUpload only covered the former.
        Result<void> allowed = mClient->checkUpload(parentHandle, parentIsRoot);
        if (!allowed.success)
        {
            std::function<void(UploadJob)> onJobFinished;
            UploadJob snapshot;
            {
                std::lock_guard<std::mutex> lock(mMutex);
                if (mQueue.empty())
                {
                    mAdvancing = false; // every exit past the flag must clear it
                    return;
                }
                mQueue.front().state = UploadState::Failed;
                mQueue.front().errorMessage = allowed.errorMessage;
                mQueue.front().errorCode = allowed.errorCode;
                snapshot = mQueue.front();
                onJobFinished = mOnJobFinished;
                mQueue.erase(mQueue.begin());
            }
            if (onJobFinished)
                onJobFinished(snapshot);
            continue;
        }

        mClient->upload(
            localPath,
            parentHandle,
            parentIsRoot,
            [this](std::uint64_t transferred, std::uint64_t total) {
                std::function<void(UploadJob)> onProgress;
                UploadJob snapshot;
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
            [this](Result<UploadOutcome> result) {
                std::function<void(UploadJob)> onJobFinished;
                UploadJob snapshot;
                {
                    std::lock_guard<std::mutex> lock(mMutex);
                    if (mQueue.empty())
                        return;
                    mQueue.front().state =
                        result.success ? UploadState::Completed : UploadState::Failed;
                    if (result.success)
                    {
                        mQueue.front().nodeHandle = result.value().nodeHandle;
                    }
                    else
                    {
                        mQueue.front().errorMessage = result.errorMessage;
                        mQueue.front().errorCode = result.errorCode;
                    }
                    snapshot = mQueue.front();
                    onJobFinished = mOnJobFinished;
                    mQueue.erase(mQueue.begin());
                }
                if (onJobFinished)
                    onJobFinished(snapshot);
                startNextIfIdle(); // auto-advance; mMutex isn't held here
            });

        // A synchronous failure has already run the whole onDone above by now,
        // and its startNextIfIdle() only set the flag -- so keep looping here
        // instead of letting it recurse. A genuinely in-flight transfer leaves
        // the flag clear and this call ends. Both branches must share one lock:
        // splitting them lets a completion land in between, set the flag, and
        // find nobody left to act on it.
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mAdvanceRequested)
        {
            mAdvancing = false;
            return;
        }
    }
}
