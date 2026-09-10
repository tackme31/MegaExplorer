#pragma once
#include "IMegaClient.h"

#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

enum class DownloadState
{
    Queued,
    Active,
    Completed,
    Failed,
    // User-initiated stop, not an error: either the transfer was aborted mid-flight
    // or the job never started. Kept apart from Failed so consumers can stay silent
    // about an outcome the user asked for.
    Cancelled,
};

struct DownloadJob
{
    std::uint64_t id = 0; // stable for this DownloadService's lifetime
    std::uint64_t handle = 0;
    // Empty when the job fetches the node itself; otherwise the part of it that is
    // fetched (an archive entry's path), which is what keeps the already-queued guard
    // from confusing two entries of one archive, or an entry with the archive.
    std::string subPath;
    std::string name;
    std::string destinationPath;   // requested path, passed to IMegaClient::download
    std::string resolvedLocalPath; // actual local path once Completed; may differ
                                   // from destinationPath (collision-rename); empty
                                   // until then
    DownloadState state = DownloadState::Queued;
    std::uint64_t transferredBytes = 0;
    std::uint64_t totalBytes = 0; // seeded from enqueue()'s expectedTotalBytes,
                                  // overwritten once real onProgress data arrives
    std::string errorMessage;     // only meaningful when state == Failed
    int errorCode = 0; // only meaningful when state == Failed; mirrors Result<T>::errorCode
};

// How one job moves its bytes. enqueue() builds the one that drives
// IMegaClient::download; enqueueRunner() puts any other transfer (extracting an
// archive entry) on the same queue, slot limit, cancel and progress reporting.
class DownloadRunner
{
public:
    virtual ~DownloadRunner() = default;

    // Called once, when the job gets a slot. The callbacks may fire on any thread and
    // before start() returns; onDone exactly once, unless start() throws -- a throw is
    // the service's cue to report the job Failed itself (ignored if onDone already ran).
    //
    // The service may drop its last reference to the runner from inside onDone. A runner
    // calling onDone from its own thread must hold a reference to itself across that
    // call and must not join that thread from its destructor.
    virtual void
    start(std::function<void(std::uint64_t transferred, std::uint64_t total)> onProgress,
          std::function<void(Result<DownloadOutcome>)> onDone) = 0;

    // Asks a started job to stop; it still reports through onDone. Must tolerate being
    // called more than once, from any thread, and after onDone already ran. One that
    // lands while start() is still running may be missed: the service repeats it once
    // start() returns.
    virtual void cancel() = 0;
};

// Runs up to kMaxConcurrent jobs: a job starts immediately while there is a free
// slot, otherwise waits for one to open.
//
// Unlike the other core services this one's state really is touched from two
// threads: enqueue() comes from the GUI thread while a runner's callbacks may
// fire from an SDK-internal (or the runner's own) thread. The invariant that keeps that safe is
// enforced structurally rather than by convention: a callback only ever writes to its own job,
// because the id is captured by value at start and looked up in mActive on arrival. cancel(jobId)
// needs this, since cancelling lets the SDK deliver a late callback that would otherwise corrupt
// whichever job was promoted in its place.
//
// Completed/failed jobs are dropped once their onJobFinished fires -- this is a
// live queue, not a download history.
class DownloadService
{
public:
    // How many transfers this service lets run at once. Chosen small on purpose:
    // MEGA's per-connection throughput is the bottleneck rather than the client, so
    // a handful of streams saturates a home link while keeping each row's progress
    // readable and the SDK's connection pool out of trouble.
    static constexpr std::size_t kMaxConcurrent = 4;

    explicit DownloadService(std::shared_ptr<IMegaClient> client);

    // Turns a MEGA node name into something that can only be a leaf inside the
    // caller's directory. Node names are server-side data and MEGA is end-to-end
    // encrypted, so nothing upstream validates them: "..\\..\\evil.exe" would
    // otherwise escape the download directory once concatenated. No length cap on
    // purpose -- truncating invents collisions, and an over-long path is an
    // OS-level error rather than a trust-boundary break.
    static std::string safeLocalFileName(const std::string& nodeName);

    // destinationPath is an exact local file path, already resolved by the caller
    // (whose leaf name has been through safeLocalFileName). expectedTotalBytes seeds
    // totalBytes from FileEntry::sizeBytes, so a consumer reading currentJob() right
    // after this call already has a real denominator. The returned id is what the
    // service matches its own SDK callbacks against.
    std::uint64_t enqueue(std::uint64_t handle,
                          const std::string& name,
                          const std::string& destinationPath,
                          std::uint64_t expectedTotalBytes);

    // A job that runs itself; runner must not be null. handle + subPath are its identity for
    // hasJobForHandle(); destinationPath is only what the job reports, since where the
    // bytes land is the runner's business.
    std::uint64_t enqueueRunner(std::uint64_t handle,
                                const std::string& subPath,
                                const std::string& name,
                                const std::string& destinationPath,
                                std::uint64_t expectedTotalBytes,
                                std::shared_ptr<DownloadRunner> runner);

    // The longest-running active job, or nothing when none is running. A copy under
    // the lock, not a reference: the completion callback runs on an SDK thread and
    // can drop it at any moment.
    std::optional<DownloadJob> currentJob() const;

    // The full live queue: active jobs first, in the order they started, then pending
    // ones in enqueue order.
    std::vector<DownloadJob> jobs() const;

    // Empties the queue: pending jobs are dropped without ever starting and every
    // active transfer is aborted through its runner. Every dropped
    // job still reports through onJobFinished exactly once, with state Cancelled --
    // callers counting jobs in flight must not have to special-case this.
    //
    // The active job's Cancelled arrives later, from the SDK thread, since only the
    // SDK can say when its transfer actually stopped.
    void cancelAll();

    // Stops one job by the id enqueue() returned, leaving the rest of the queue
    // running. A pending job never starts and reports Cancelled from inside this
    // call; the active one is aborted through its runner and
    // reports later from the SDK thread, exactly as cancelAll() does. An id that
    // already finished is a no-op, so a stale row in the UI cannot stop anything.
    void cancel(std::uint64_t jobId);

    // The already-queued guard, without jobs()'s whole-queue copy -- that would be
    // O(N^2) when a caller loops it once per handle to bulk-enqueue a selection.
    // Matches handle and subPath together: an empty subPath means the node itself.
    bool hasJobForHandle(std::uint64_t handle, const std::string& subPath = {}) const;

    // Observers rather than a Result<T> per call: this service is persistent and
    // multi-call, so callers need notifications for whichever job is active.
    // Single-subscriber -- registering again replaces the previous callback.
    void setOnProgress(std::function<void(DownloadJob)> onProgress);
    void setOnJobFinished(std::function<void(DownloadJob)> onJobFinished);

private:
    // The runner stays out of DownloadJob so snapshots handed to observers can't
    // reach into a live transfer.
    struct Entry
    {
        DownloadJob job;
        std::shared_ptr<DownloadRunner> runner;
    };

    std::uint64_t enqueueEntry(DownloadJob job, std::shared_ptr<DownloadRunner> runner);

    // Fills every free slot from the front of mPending, then keeps going while jobs
    // keep finishing inside this call. Takes mMutex itself rather than requiring the
    // caller to hold it: a runner can run onDone before start() returns, so holding the
    // lock across it would self-deadlock.
    void startNextIfIdle();

    // mMutex must be held. Null once the job has finished and been dropped.
    Entry* activeEntry(std::uint64_t jobId);

    // mMutex must be held. Frees the slot and forgets any cancel asked for it.
    // Invalidates every pointer activeEntry() handed out.
    void dropActive(std::uint64_t jobId);

    std::shared_ptr<IMegaClient> mClient;
    mutable std::mutex mMutex;
    std::uint64_t mNextId = 1;
    std::vector<Entry> mActive; // in-flight, at most kMaxConcurrent, start order
    std::deque<Entry> mPending; // not yet started, in enqueue order
    std::function<void(DownloadJob)> mOnProgress;
    std::function<void(DownloadJob)> mOnJobFinished;

    // Re-entrancy guard for startNextIfIdle(): an in-stack failure makes onDone's
    // auto-advance nest one frame per queued job -- 200 selected files against a
    // folder deleted elsewhere is 200 frames, on a thread whose stack size this app
    // doesn't control. A nested call returns immediately instead; the loop that owns
    // the flag re-reads the queue every turn, so it picks the work up anyway.
    bool mAdvancing = false;

    // Ids whose cancel may not have reached the runner yet. A job is in mActive before
    // its runner's start() has created anything to cancel (IMegaClient::download's
    // cancel token), so a cancel landing in that window does nothing;
    // startNextIfIdle() re-asserts it from here once the call returns. Entries are
    // dropped when the job finishes.
    std::set<std::uint64_t> mCancelRequested;
};
