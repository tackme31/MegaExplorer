#include "UploadService.h"

#include "MegaErrorCodes.h"

#include <algorithm>

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

UploadService::UploadService(std::shared_ptr<IMegaClient> client) : mClient(std::move(client)) {}

std::uint64_t UploadService::enqueue(const std::string& localPath,
                                     const std::string& name,
                                     std::uint64_t parentHandle,
                                     bool parentIsRoot,
                                     std::uint64_t expectedTotalBytes)
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
        job.totalBytes = expectedTotalBytes;
        id = job.id;
        mPending.push_back(std::move(job));
    }
    startNextIfIdle();
    return id;
}

UploadJob* UploadService::activeJob(std::uint64_t jobId)
{
    for (UploadJob& job : mActive)
    {
        if (job.id == jobId)
            return &job;
    }
    return nullptr;
}

void UploadService::dropActive(std::uint64_t jobId)
{
    mActive.erase(std::remove_if(mActive.begin(),
                                 mActive.end(),
                                 [jobId](const UploadJob& job) {
                                     return job.id == jobId;
                                 }),
                  mActive.end());
    mCancelRequested.erase(jobId);
}

std::optional<UploadJob> UploadService::currentJob() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mActive.empty())
        return std::nullopt;
    return mActive.front();
}

std::vector<UploadJob> UploadService::jobs() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    std::vector<UploadJob> all;
    all.reserve(mPending.size() + mActive.size());
    all.insert(all.end(), mActive.begin(), mActive.end());
    all.insert(all.end(), mPending.begin(), mPending.end());
    return all;
}

std::size_t UploadService::queueLength() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mPending.size() + mActive.size();
}

void UploadService::cancelAll()
{
    std::deque<UploadJob> dropped;
    std::vector<std::uint64_t> activeIds;
    std::function<void(UploadJob)> onJobFinished;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        dropped.swap(mPending);
        for (const UploadJob& job : mActive)
        {
            activeIds.push_back(job.id);
            mCancelRequested.insert(job.id);
        }
        onJobFinished = mOnJobFinished;
    }

    for (std::uint64_t id : activeIds)
        mClient->cancelUpload(id);

    if (!onJobFinished)
        return;
    for (UploadJob& job : dropped)
    {
        job.state = UploadState::Cancelled;
        onJobFinished(job);
    }
}

void UploadService::cancel(std::uint64_t jobId)
{
    std::optional<UploadJob> dropped;
    std::function<void(UploadJob)> onJobFinished;
    bool wasActive = false;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        onJobFinished = mOnJobFinished;
        if (activeJob(jobId))
        {
            mCancelRequested.insert(jobId);
            wasActive = true;
        }
        else
        {
            for (auto it = mPending.begin(); it != mPending.end(); ++it)
            {
                if (it->id != jobId)
                    continue;
                dropped = std::move(*it);
                mPending.erase(it);
                break;
            }
        }
    }

    // Outside the lock, for the same reason as DownloadService::cancel().
    if (wasActive)
    {
        mClient->cancelUpload(jobId);
        return;
    }

    if (!dropped || !onJobFinished)
        return;
    dropped->state = UploadState::Cancelled;
    onJobFinished(*dropped);
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
    // guard turns into another turn here rather than a nested frame.
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mAdvancing)
            return;
        mAdvancing = true;
    }
    AdvancingGuard advancing = {mMutex, mAdvancing};

    for (;;)
    {
        std::uint64_t id;
        std::string localPath;
        std::uint64_t parentHandle;
        bool parentIsRoot;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (mActive.size() >= kMaxConcurrent || mPending.empty())
            {
                advancing.clearHeld();
                return;
            }
            UploadJob job = std::move(mPending.front());
            mPending.pop_front();
            job.state = UploadState::Active;
            id = job.id;
            localPath = job.localPath;
            parentHandle = job.parentHandle;
            parentIsRoot = job.parentIsRoot;
            mActive.push_back(std::move(job));
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
                // Nothing can steal this entry here -- no transfer has started, so no
                // SDK thread knows this job yet -- but looked up anyway, so no path
                // writes mActive without naming which job it means.
                UploadJob* job = activeJob(id);
                if (!job)
                {
                    advancing.clearHeld(); // every exit past the flag must clear it
                    return;
                }
                // A cancel that landed during the checkUpload() round-trip named a
                // transfer that had not started, so it reached nothing and nothing
                // else will report it. Calling the rejection a failure here would
                // count the user's own stop as an error and stack a second toast on
                // the one cancelAll() already raised.
                job->state =
                    mCancelRequested.count(id) != 0 ? UploadState::Cancelled : UploadState::Failed;
                job->errorMessage = allowed.errorMessage;
                job->errorCode = allowed.errorCode;
                snapshot = *job;
                onJobFinished = mOnJobFinished;
                dropActive(id);
            }
            if (onJobFinished)
                onJobFinished(snapshot);
            continue;
        }

        try
        {
            mClient->upload(
                localPath,
                parentHandle,
                parentIsRoot,
                id,
                [this, id](std::uint64_t transferred, std::uint64_t total) {
                    std::function<void(UploadJob)> onProgress;
                    UploadJob snapshot;
                    {
                        std::lock_guard<std::mutex> lock(mMutex);
                        // Not ours: this job already finished (or was cancelled) and
                        // the SDK is still delivering. Writing here would land on
                        // whichever job was promoted in its place.
                        UploadJob* job = activeJob(id);
                        if (!job)
                            return;
                        job->transferredBytes = transferred;
                        job->totalBytes = total;
                        snapshot = *job;
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
                        UploadJob* job = activeJob(id);
                        if (!job)
                            return; // same as onProgress above
                        // Same kEIncomplete and same cancel-wins rule as DownloadService
                        // -- see the comment there.
                        job->state = result.success ? UploadState::Completed
                                     : (result.errorCode == MegaErrorCode::kEIncomplete ||
                                        mCancelRequested.count(id) != 0)
                                         ? UploadState::Cancelled
                                         : UploadState::Failed;
                        if (result.success)
                        {
                            job->nodeHandle = result.value().nodeHandle;
                        }
                        else
                        {
                            job->errorMessage = result.errorMessage;
                            job->errorCode = result.errorCode;
                        }
                        snapshot = *job;
                        onJobFinished = mOnJobFinished;
                        dropActive(id);
                    }
                    if (onJobFinished)
                        onJobFinished(snapshot);
                    startNextIfIdle(); // auto-advance; mMutex isn't held here
                });
        }
        catch (...)
        {
            // Nothing has run this job's completion, so the slot it took in mActive and
            // the batch counting down on that callback both leak unless it is reported
            // here. Ids are never reused, so finding it still there means this start threw.
            std::function<void(UploadJob)> onJobFinished;
            UploadJob snapshot;
            {
                std::lock_guard<std::mutex> lock(mMutex);
                UploadJob* job = activeJob(id);
                if (job)
                {
                    // Same cancel-wins rule as the paths above.
                    job->state = mCancelRequested.count(id) != 0 ? UploadState::Cancelled
                                                                 : UploadState::Failed;
                    job->errorMessage = "The upload could not be started";
                    snapshot = *job;
                    onJobFinished = mOnJobFinished;
                    dropActive(id);
                }
            }
            if (onJobFinished)
                onJobFinished(snapshot);
            throw;
        }

        bool cancelRaced = false;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            // Same re-assert as DownloadService, and the window is wider here: the
            // blocking checkUpload() above sits inside it too.
            cancelRaced = activeJob(id) != nullptr && mCancelRequested.count(id) != 0;
        }
        if (cancelRaced)
            mClient->cancelUpload(id);
    }
}
