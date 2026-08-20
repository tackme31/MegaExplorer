#include "DownloadService.h"

#include "MegaErrorCodes.h"

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

std::optional<DownloadJob> DownloadService::currentJob() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mActive;
}

std::vector<DownloadJob> DownloadService::jobs() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    std::vector<DownloadJob> all;
    all.reserve(mPending.size() + (mActive ? 1 : 0));
    if (mActive)
        all.push_back(*mActive);
    all.insert(all.end(), mPending.begin(), mPending.end());
    return all;
}

void DownloadService::cancelAll()
{
    std::deque<DownloadJob> dropped;
    std::function<void(DownloadJob)> onJobFinished;
    bool hadActive = false;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        ++mCancelGeneration;
        dropped.swap(mPending);
        hadActive = mActive.has_value();
        onJobFinished = mOnJobFinished;
    }

    // Order matters: the queue is already empty by the time the abort can come back,
    // so the active job's onDone cannot promote anything behind it.
    if (hadActive)
        mClient->cancelDownload();

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
    {
        std::lock_guard<std::mutex> lock(mMutex);
        onJobFinished = mOnJobFinished;
        if (mActive && mActive->id == jobId)
        {
            // Only an active-job cancel bumps the generation: it is what
            // startNextIfIdle()'s re-assert reads, so bumping it for a pending job
            // would abort whichever transfer happened to be starting just then.
            ++mCancelGeneration;
            // Held across the client call, unlike cancelAll(): the rest of the queue
            // is still here, so releasing first would let this job finish and promote
            // the next one straight into the abort. Safe only because cancelDownload()
            // just sets a shared flag and re-enters nothing -- don't hoist a call that
            // does more out here.
            mClient->cancelDownload();
            return;
        }
        for (auto it = mPending.begin(); it != mPending.end(); ++it)
        {
            if (it->id != jobId)
                continue;
            dropped = std::move(*it);
            mPending.erase(it);
            break;
        }
    }

    if (!dropped || !onJobFinished)
        return;
    dropped->state = DownloadState::Cancelled;
    onJobFinished(*dropped);
}

bool DownloadService::hasJobForHandle(std::uint64_t handle) const
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mActive && mActive->handle == handle)
        return true;
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
        {
            mAdvanceRequested = true; // whoever is in the loop below picks it up
            return;
        }
        mAdvancing = true;
    }

    for (;;)
    {
        std::uint64_t id;
        std::uint64_t handle;
        std::string destinationPath;
        std::uint64_t generationAtStart;
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
            mActive->state = DownloadState::Active;
            id = mActive->id;
            handle = mActive->handle;
            destinationPath = mActive->destinationPath;
            generationAtStart = mCancelGeneration;
        }

        mClient->download(
            handle,
            destinationPath,
            [this, id](std::uint64_t transferred, std::uint64_t total) {
                std::function<void(DownloadJob)> onProgress;
                DownloadJob snapshot;
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
            [this, id](Result<DownloadOutcome> result) {
                std::function<void(DownloadJob)> onJobFinished;
                DownloadJob snapshot;
                {
                    std::lock_guard<std::mutex> lock(mMutex);
                    if (!mActive || mActive->id != id)
                        return; // same as onProgress above
                    // kEIncomplete is the SDK's own marker for an aborted transfer
                    // (it sets STATE_CANCELLED on exactly that code), so it is the
                    // one failure that is not an error to report.
                    mActive->state = result.success ? DownloadState::Completed
                                     : result.errorCode == MegaErrorCode::kEIncomplete
                                         ? DownloadState::Cancelled
                                         : DownloadState::Failed;
                    if (result.success)
                    {
                        mActive->resolvedLocalPath = result.value().localPath;
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
        bool cancelRaced = false;
        bool finished = false;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            // A cancelAll() during the download() call above could only reach the
            // token of whatever ran before it -- this job's is created inside that
            // call. Re-assert the cancel now that it exists, or the user's click
            // silently does nothing and the transfer completes.
            cancelRaced = mActive.has_value() && mCancelGeneration != generationAtStart;
            if (!mAdvanceRequested)
            {
                mAdvancing = false;
                finished = true;
            }
        }
        if (cancelRaced)
            mClient->cancelDownload();
        if (finished)
            return;
    }
}
