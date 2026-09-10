#include "DownloadService.h"

#include "MegaErrorCodes.h"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace
{

// Puts the re-entrancy flag back when startNextIfIdle()'s loop leaves through an
// exception: everything it calls out to (IMegaClient, the finished callback, the
// controllers and QML behind it) can throw, and a flag left set makes every later
// startNextIfIdle() return at the guard, so enqueue() keeps handing out ids while no
// transfer ever starts again.
//
// The ordinary exits call clearHeld() instead of leaving it to the destructor: the
// flag has to drop in the same critical section that decided to stop, or an enqueue
// landing in between bounces off the guard and is left for nobody to start.
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

// The job id doubles as the client's transferId, so a cancel can name the transfer
// before download() has returned.
class ClientDownloadRunner final : public DownloadRunner
{
public:
    ClientDownloadRunner(std::shared_ptr<IMegaClient> client,
                         std::uint64_t handle,
                         std::string destinationPath,
                         std::uint64_t jobId)
        : mClient(std::move(client)), mHandle(handle), mDestinationPath(std::move(destinationPath)),
          mJobId(jobId)
    {}

    void start(std::function<void(std::uint64_t, std::uint64_t)> onProgress,
               std::function<void(Result<DownloadOutcome>)> onDone) override
    {
        mClient->download(
            mHandle, mDestinationPath, mJobId, std::move(onProgress), std::move(onDone));
    }

    void cancel() override
    {
        mClient->cancelDownload(mJobId);
    }

private:
    std::shared_ptr<IMegaClient> mClient;
    std::uint64_t mHandle;
    std::string mDestinationPath;
    std::uint64_t mJobId;
};

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
    DownloadJob job;
    job.handle = handle;
    job.name = name;
    job.destinationPath = destinationPath;
    job.totalBytes = expectedTotalBytes;
    return enqueueEntry(std::move(job), nullptr);
}

std::uint64_t DownloadService::enqueueRunner(std::uint64_t handle,
                                             const std::string& subPath,
                                             const std::string& name,
                                             const std::string& destinationPath,
                                             std::uint64_t expectedTotalBytes,
                                             std::shared_ptr<DownloadRunner> runner)
{
    // enqueueEntry() reads null as the plain download, which would fetch the whole node.
    if (!runner)
        throw std::invalid_argument("enqueueRunner needs a runner");
    DownloadJob job;
    job.handle = handle;
    job.subPath = subPath;
    job.name = name;
    job.destinationPath = destinationPath;
    job.totalBytes = expectedTotalBytes;
    return enqueueEntry(std::move(job), std::move(runner));
}

std::uint64_t DownloadService::enqueueEntry(DownloadJob job, std::shared_ptr<DownloadRunner> runner)
{
    std::uint64_t id;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        job.id = mNextId++;
        id = job.id;
        // Null is the plain download, built here because its runner needs the id.
        if (!runner)
            runner = std::make_shared<ClientDownloadRunner>(
                mClient, job.handle, job.destinationPath, job.id);
        mPending.push_back(Entry{std::move(job), std::move(runner)});
    }
    startNextIfIdle();
    return id;
}

DownloadService::Entry* DownloadService::activeEntry(std::uint64_t jobId)
{
    for (Entry& entry : mActive)
    {
        if (entry.job.id == jobId)
            return &entry;
    }
    return nullptr;
}

void DownloadService::dropActive(std::uint64_t jobId)
{
    mActive.erase(std::remove_if(mActive.begin(),
                                 mActive.end(),
                                 [jobId](const Entry& entry) {
                                     return entry.job.id == jobId;
                                 }),
                  mActive.end());
    mCancelRequested.erase(jobId);
}

std::optional<DownloadJob> DownloadService::currentJob() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mActive.empty())
        return std::nullopt;
    return mActive.front().job;
}

std::vector<DownloadJob> DownloadService::jobs() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    std::vector<DownloadJob> all;
    all.reserve(mPending.size() + mActive.size());
    for (const Entry& entry : mActive)
        all.push_back(entry.job);
    for (const Entry& entry : mPending)
        all.push_back(entry.job);
    return all;
}

void DownloadService::cancelAll()
{
    std::deque<Entry> dropped;
    std::vector<std::shared_ptr<DownloadRunner>> activeRunners;
    std::function<void(DownloadJob)> onJobFinished;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        dropped.swap(mPending);
        for (const Entry& entry : mActive)
        {
            activeRunners.push_back(entry.runner);
            mCancelRequested.insert(entry.job.id);
        }
        onJobFinished = mOnJobFinished;
    }

    // Order matters: the queue is already empty by the time the aborts can come back,
    // so no active job's onDone can promote anything behind it.
    for (const std::shared_ptr<DownloadRunner>& runner : activeRunners)
        runner->cancel();

    if (!onJobFinished)
        return;
    for (Entry& entry : dropped)
    {
        entry.job.state = DownloadState::Cancelled;
        onJobFinished(entry.job);
    }
}

void DownloadService::cancel(std::uint64_t jobId)
{
    std::optional<DownloadJob> dropped;
    std::function<void(DownloadJob)> onJobFinished;
    std::shared_ptr<DownloadRunner> activeRunner;
    std::shared_ptr<DownloadRunner> released;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        onJobFinished = mOnJobFinished;
        if (Entry* entry = activeEntry(jobId))
        {
            mCancelRequested.insert(jobId);
            activeRunner = entry->runner;
        }
        else
        {
            for (auto it = mPending.begin(); it != mPending.end(); ++it)
            {
                if (it->job.id != jobId)
                    continue;
                dropped = std::move(it->job);
                released = std::move(it->runner); // destroyed outside the lock
                mPending.erase(it);
                break;
            }
        }
    }

    // Outside the lock, unlike the whole-direction cancel this replaced: the abort
    // names one transfer, so a job finishing in the meantime cannot let its successor
    // be promoted into it.
    if (activeRunner)
    {
        activeRunner->cancel();
        return;
    }

    if (!dropped || !onJobFinished)
        return;
    dropped->state = DownloadState::Cancelled;
    onJobFinished(*dropped);
}

bool DownloadService::hasJobForHandle(std::uint64_t handle, const std::string& subPath) const
{
    const auto matches = [&](const Entry& entry) {
        return entry.job.handle == handle && entry.job.subPath == subPath;
    };
    std::lock_guard<std::mutex> lock(mMutex);
    return std::any_of(mActive.begin(), mActive.end(), matches) ||
           std::any_of(mPending.begin(), mPending.end(), matches);
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
    AdvancingGuard advancing = {mMutex, mAdvancing};

    for (;;)
    {
        std::uint64_t id;
        std::shared_ptr<DownloadRunner> runner;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (mActive.size() >= kMaxConcurrent || mPending.empty())
            {
                advancing.clearHeld();
                return;
            }
            Entry entry = std::move(mPending.front());
            mPending.pop_front();
            entry.job.state = DownloadState::Active;
            id = entry.job.id;
            runner = entry.runner;
            mActive.push_back(std::move(entry));
        }

        try
        {
            runner->start(
                [this, id](std::uint64_t transferred, std::uint64_t total) {
                    std::function<void(DownloadJob)> onProgress;
                    DownloadJob snapshot;
                    {
                        std::lock_guard<std::mutex> lock(mMutex);
                        // Not ours: this job already finished (or was cancelled) and
                        // the SDK is still delivering. Writing here would land on
                        // whichever job was promoted in its place.
                        Entry* entry = activeEntry(id);
                        if (!entry)
                            return;
                        DownloadJob* job = &entry->job;
                        job->transferredBytes = transferred;
                        job->totalBytes = total;
                        snapshot = *job;
                        onProgress = mOnProgress;
                    }
                    if (onProgress)
                        onProgress(snapshot);
                },
                [this, id](Result<DownloadOutcome> result) {
                    // Declared first so the runner, maybe the last reference, dies after
                    // everything below and outside mMutex (its destructor may join a thread).
                    std::shared_ptr<DownloadRunner> released;
                    std::function<void(DownloadJob)> onJobFinished;
                    DownloadJob snapshot;
                    {
                        std::lock_guard<std::mutex> lock(mMutex);
                        Entry* entry = activeEntry(id);
                        if (!entry)
                            return; // same as onProgress above
                        DownloadJob* job = &entry->job;
                        // kEIncomplete is the SDK's own marker for an aborted transfer
                        // (it sets STATE_CANCELLED on exactly that code), so it is the
                        // one failure that is not an error to report. A cancel this
                        // service asked for reads the same way even when the runner never
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
                        released = std::move(entry->runner);
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
            // anything counting down on that callback both leak unless it is reported
            // here. Ids are never reused, so finding it still there means this start threw.
            std::function<void(DownloadJob)> onJobFinished;
            DownloadJob snapshot;
            {
                std::lock_guard<std::mutex> lock(mMutex);
                if (Entry* entry = activeEntry(id))
                {
                    DownloadJob* job = &entry->job;
                    // Same cancel-wins rule as the completion above.
                    job->state = mCancelRequested.count(id) != 0 ? DownloadState::Cancelled
                                                                 : DownloadState::Failed;
                    job->errorMessage = "The download could not be started";
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
            // A cancel arriving during start() above may have found nothing to cancel
            // yet. Re-assert it now that the transfer exists, or the user's click
            // silently does nothing and the transfer completes.
            cancelRaced = activeEntry(id) != nullptr && mCancelRequested.count(id) != 0;
        }
        if (cancelRaced)
            runner->cancel();
    }
}
