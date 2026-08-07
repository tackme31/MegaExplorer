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
        id = job.id;
        mPending.push_back(std::move(job));
    }
    startNextIfIdle();
    return id;
}

std::optional<UploadJob> UploadService::currentJob() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mActive;
}

std::vector<UploadJob> UploadService::jobs() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    std::vector<UploadJob> all;
    all.reserve(mPending.size() + (mActive ? 1 : 0));
    if (mActive)
        all.push_back(*mActive);
    all.insert(all.end(), mPending.begin(), mPending.end());
    return all;
}

std::size_t UploadService::queueLength() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mPending.size() + (mActive ? 1 : 0);
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
    // Two synchronous failure paths feed this loop, both reachable in production
    // (200 files dropped onto a folder just deleted elsewhere): the destination
    // re-validation below, and upload() itself when the parent no longer resolves.
    // The second returns through onDone's startNextIfIdle(), which the mAdvancing
    // trampoline turns into another turn here rather than a nested frame.
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
        std::uint64_t id;
        std::string localPath;
        std::uint64_t parentHandle;
        bool parentIsRoot;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mAdvanceRequested = false;
            if (mActive || mPending.empty())
            {
                mAdvancing = false;
                return;
            }
            mActive = std::move(mPending.front());
            mPending.pop_front();
            mActive->state = UploadState::Active;
            id = mActive->id;
            localPath = mActive->localPath;
            parentHandle = mActive->parentHandle;
            parentIsRoot = mActive->parentIsRoot;
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
                // Nothing can steal mActive here -- no transfer has started, so no
                // SDK thread knows this job yet -- but checked anyway, so no path
                // writes mActive without naming which job it means.
                if (!mActive || mActive->id != id)
                {
                    mAdvancing = false; // every exit past the flag must clear it
                    return;
                }
                mActive->state = UploadState::Failed;
                mActive->errorMessage = allowed.errorMessage;
                mActive->errorCode = allowed.errorCode;
                snapshot = *mActive;
                onJobFinished = mOnJobFinished;
                mActive.reset();
            }
            if (onJobFinished)
                onJobFinished(snapshot);
            continue;
        }

        mClient->upload(
            localPath,
            parentHandle,
            parentIsRoot,
            [this, id](std::uint64_t transferred, std::uint64_t total) {
                std::function<void(UploadJob)> onProgress;
                UploadJob snapshot;
                {
                    std::lock_guard<std::mutex> lock(mMutex);
                    // Not ours: this job already finished (or was cancelled) and
                    // the SDK is still delivering. Writing here would land on
                    // whichever job was promoted in its place.
                    if (!mActive || mActive->id != id)
                        return;
                    mActive->transferredBytes = transferred;
                    mActive->totalBytes = total;
                    snapshot = *mActive;
                    onProgress = mOnProgress;
                }
                if (onProgress)
                    onProgress(snapshot);
            },
            [this, id](Result<UploadOutcome> result) {
                std::function<void(UploadJob)> onJobFinished;
                UploadJob snapshot;
                {
                    std::lock_guard<std::mutex> lock(mMutex);
                    if (!mActive || mActive->id != id)
                        return; // same as onProgress above
                    mActive->state = result.success ? UploadState::Completed : UploadState::Failed;
                    if (result.success)
                    {
                        mActive->nodeHandle = result.value().nodeHandle;
                    }
                    else
                    {
                        mActive->errorMessage = result.errorMessage;
                        mActive->errorCode = result.errorCode;
                    }
                    snapshot = *mActive;
                    onJobFinished = mOnJobFinished;
                    mActive.reset();
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
