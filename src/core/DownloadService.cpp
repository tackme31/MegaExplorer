#include "DownloadService.h"

#include "MegaErrorCodes.h"

#include <algorithm>
#include <array>

namespace
{

bool isAsciiLetter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isForbiddenChar(char c)
{
    // Bytes >= 0x80 are left alone so UTF-8 sequences survive intact.
    if (static_cast<unsigned char>(c) < 0x20)
        return true;
    switch (c)
    {
        case '<':
        case '>':
        case ':':
        case '"':
        case '|':
        case '?':
        case '*':
            return true;
        default:
            return false;
    }
}

// Windows claims these whatever the extension, so "CON.txt" is as unusable as
// "CON" -- a usability guard rather than a security one, but it belongs in the
// same single rule.
bool isReservedDeviceName(const std::string& stem)
{
    static const std::array<const char*, 22> reserved = {
        "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
        "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};

    std::string upper = stem;
    for (char& c : upper)
    {
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    }
    for (const char* name : reserved)
    {
        if (upper == name)
            return true;
    }
    return false;
}

} // namespace

DownloadService::DownloadService(std::shared_ptr<IMegaClient> client) : mClient(std::move(client))
{}

std::string DownloadService::safeLocalFileName(const std::string& nodeName)
{
    // Everything up to the last separator is a directory the node name has no
    // business choosing -- this is the step that stops "..\..\evil.exe".
    const std::size_t lastSeparator = nodeName.find_last_of("/\\");
    std::string leaf =
        (lastSeparator == std::string::npos) ? nodeName : nodeName.substr(lastSeparator + 1);

    // "C:evil.exe" is drive-relative and has no separator to cut at. Must
    // happen before the ':' below turns into '_'.
    if (leaf.size() >= 2 && isAsciiLetter(leaf[0]) && leaf[1] == ':')
        leaf.erase(0, 2);

    for (char& c : leaf)
    {
        if (isForbiddenChar(c))
            c = '_';
    }

    // Windows silently drops trailing dots and spaces, so a name that relied
    // on them wouldn't round-trip. Also collapses "." / ".." / "..." to empty,
    // which the fallback below then names.
    const std::size_t lastKept = leaf.find_last_not_of(". ");
    leaf = (lastKept == std::string::npos) ? std::string() : leaf.substr(0, lastKept + 1);

    if (leaf.empty())
        return "download";

    if (isReservedDeviceName(leaf.substr(0, leaf.find('.'))))
        leaf.insert(leaf.begin(), '_');

    return leaf;
}

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
        id = job.id;
        mPending.push_back(std::move(job));
    }
    startNextIfIdle();
    return id;
}

DownloadJob* DownloadService::activeJob(std::uint64_t jobId)
{
    for (DownloadJob& job : mActive)
    {
        if (job.id == jobId)
            return &job;
    }
    return nullptr;
}

void DownloadService::dropActive(std::uint64_t jobId)
{
    mActive.erase(std::remove_if(mActive.begin(),
                                 mActive.end(),
                                 [jobId](const DownloadJob& job) {
                                     return job.id == jobId;
                                 }),
                  mActive.end());
    mCancelRequested.erase(jobId);
}

std::optional<DownloadJob> DownloadService::currentJob() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mActive.empty())
        return std::nullopt;
    return mActive.front();
}

std::vector<DownloadJob> DownloadService::jobs() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    std::vector<DownloadJob> all;
    all.reserve(mPending.size() + mActive.size());
    all.insert(all.end(), mActive.begin(), mActive.end());
    all.insert(all.end(), mPending.begin(), mPending.end());
    return all;
}

void DownloadService::cancelAll()
{
    std::deque<DownloadJob> dropped;
    std::vector<std::uint64_t> activeIds;
    std::function<void(DownloadJob)> onJobFinished;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        dropped.swap(mPending);
        for (const DownloadJob& job : mActive)
        {
            activeIds.push_back(job.id);
            mCancelRequested.insert(job.id);
        }
        onJobFinished = mOnJobFinished;
    }

    // Order matters: the queue is already empty by the time the aborts can come back,
    // so no active job's onDone can promote anything behind it.
    for (std::uint64_t id : activeIds)
        mClient->cancelDownload(id);

    if (!onJobFinished)
        return;
    for (DownloadJob& job : dropped)
    {
        job.state = DownloadState::Cancelled;
        onJobFinished(job);
    }
}

void DownloadService::cancel(std::uint64_t jobId)
{
    std::optional<DownloadJob> dropped;
    std::function<void(DownloadJob)> onJobFinished;
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

    // Outside the lock, unlike the whole-direction cancel this replaced: the abort
    // names one transfer, so a job finishing in the meantime cannot let its successor
    // be promoted into it.
    if (wasActive)
    {
        mClient->cancelDownload(jobId);
        return;
    }

    if (!dropped || !onJobFinished)
        return;
    dropped->state = DownloadState::Cancelled;
    onJobFinished(*dropped);
}

bool DownloadService::hasJobForHandle(std::uint64_t handle) const
{
    std::lock_guard<std::mutex> lock(mMutex);
    for (const DownloadJob& job : mActive)
    {
        if (job.handle == handle)
            return true;
    }
    for (const DownloadJob& job : mPending)
    {
        if (job.handle == handle)
            return true;
    }
    return false;
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
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mAdvancing)
            return; // the loop below re-reads the queue every turn and will see this
        mAdvancing = true;
    }

    for (;;)
    {
        std::uint64_t id;
        std::uint64_t handle;
        std::string destinationPath;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (mActive.size() >= kMaxConcurrent || mPending.empty())
            {
                mAdvancing = false;
                return;
            }
            DownloadJob job = std::move(mPending.front());
            mPending.pop_front();
            job.state = DownloadState::Active;
            id = job.id;
            handle = job.handle;
            destinationPath = job.destinationPath;
            mActive.push_back(std::move(job));
        }

        mClient->download(
            handle,
            destinationPath,
            id,
            [this, id](std::uint64_t transferred, std::uint64_t total) {
                std::function<void(DownloadJob)> onProgress;
                DownloadJob snapshot;
                {
                    std::lock_guard<std::mutex> lock(mMutex);
                    // Not ours: this job already finished (or was cancelled) and
                    // the SDK is still delivering. Writing here would land on
                    // whichever job was promoted in its place.
                    DownloadJob* job = activeJob(id);
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
            [this, id](Result<DownloadOutcome> result) {
                std::function<void(DownloadJob)> onJobFinished;
                DownloadJob snapshot;
                {
                    std::lock_guard<std::mutex> lock(mMutex);
                    DownloadJob* job = activeJob(id);
                    if (!job)
                        return; // same as onProgress above
                    // kEIncomplete is the SDK's own marker for an aborted transfer
                    // (it sets STATE_CANCELLED on exactly that code), so it is the
                    // one failure that is not an error to report. A cancel this
                    // service asked for reads the same way even when the SDK never
                    // saw it: the transfer can fail for its own reason before the
                    // abort reaches it, and reporting that turns the user's own stop
                    // into an error toast.
                    job->state = result.success ? DownloadState::Completed
                                 : (result.errorCode == MegaErrorCode::kEIncomplete ||
                                    mCancelRequested.count(id) != 0)
                                     ? DownloadState::Cancelled
                                     : DownloadState::Failed;
                    if (result.success)
                    {
                        job->resolvedLocalPath = result.value().localPath;
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

        bool cancelRaced = false;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            // A cancel arriving during the download() call above named a transfer the
            // client had not created yet, so it did nothing. Re-assert it now that it
            // exists, or the user's click silently does nothing and the transfer
            // completes.
            cancelRaced = activeJob(id) != nullptr && mCancelRequested.count(id) != 0;
        }
        if (cancelRaced)
            mClient->cancelDownload(id);
    }
}
