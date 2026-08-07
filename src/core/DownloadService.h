#pragma once
#include "IMegaClient.h"

#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

enum class DownloadState
{
    Queued,
    Active,
    Completed,
    Failed,
};

struct DownloadJob
{
    std::uint64_t id = 0; // stable for this DownloadService's lifetime
    std::uint64_t handle = 0;
    std::string name;
    std::string destinationPath;   // requested path, passed to IMegaClient::download
    std::string resolvedLocalPath; // actual local path once Completed; may differ
                                   // from destinationPath (collision-rename); empty
                                   // until then
    bool alreadyPresent = false;   // once Completed: true if an identical file was
                                   // already at destinationPath and the SDK skipped
                                   // the transfer instead of downloading/renaming it
    DownloadState state = DownloadState::Queued;
    std::uint64_t transferredBytes = 0;
    std::uint64_t totalBytes = 0; // seeded from enqueue()'s expectedTotalBytes,
                                  // overwritten once real onProgress data arrives
    std::string errorMessage;     // only meaningful when state == Failed
    int errorCode = 0; // only meaningful when state == Failed; mirrors Result<T>::errorCode
};

// Serializes downloads one at a time over IMegaClient::download: a job starts
// immediately if nothing is active, otherwise waits for everything ahead of it to
// finish (succeed or fail).
//
// Unlike the other core services this one's state really is touched from two
// threads: enqueue() comes from the GUI thread while download()'s callbacks may
// fire from an SDK-internal thread. Two invariants keep that safe, both enforced
// structurally rather than by convention:
//
//  - At most one active job, because mActive is an optional rather than a position
//    in a container -- no code can mistake a waiting job for the running one.
//  - A callback only writes to its own job: the id is captured by value at start
//    and re-checked against mActive->id on arrival. A future cancel(jobId) needs
//    this, since cancelling lets the SDK deliver a late callback that would
//    otherwise corrupt whichever job was promoted in its place.
//
// Completed/failed jobs are dropped once their onJobFinished fires -- this is a
// live queue, not a download history.
class DownloadService
{
public:
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

    // A copy under the lock, not a reference: the completion callback runs on an SDK
    // thread and can clear mActive at any moment.
    std::optional<DownloadJob> currentJob() const;

    // The full live queue: active job first, then pending ones in enqueue order.
    std::vector<DownloadJob> jobs() const;

    // The already-queued guard, without jobs()'s whole-queue copy -- that would be
    // O(N^2) when a caller loops it once per handle to bulk-enqueue a selection.
    bool hasJobForHandle(std::uint64_t handle) const;

    // Observers rather than a Result<T> per call: this service is persistent and
    // multi-call, so callers need notifications for whichever job is active.
    // Single-subscriber -- registering again replaces the previous callback.
    void setOnProgress(std::function<void(DownloadJob)> onProgress);
    void setOnJobFinished(std::function<void(DownloadJob)> onJobFinished);

private:
    // Promotes mPending.front() into mActive, then keeps going while jobs keep
    // finishing inside this call. Takes mMutex itself rather than requiring the
    // caller to hold it: download() can run onDone before it returns, so holding the
    // lock across it would self-deadlock.
    void startNextIfIdle();

    std::shared_ptr<IMegaClient> mClient;
    mutable std::mutex mMutex;
    std::uint64_t mNextId = 1;
    std::optional<DownloadJob> mActive;  // the one in-flight job, if any
    std::deque<DownloadJob> mPending;    // not yet started, in enqueue order
    std::function<void(DownloadJob)> mOnProgress;
    std::function<void(DownloadJob)> mOnJobFinished;

    // Re-entrancy trampoline for startNextIfIdle(): an in-stack failure makes
    // onDone's auto-advance nest one frame per queued job -- 200 selected files
    // against a folder deleted elsewhere is 200 frames, on a thread whose stack size
    // this app doesn't control. A nested call now just asks the running loop to go
    // round again. Both flags live under mMutex.
    bool mAdvancing = false;
    bool mAdvanceRequested = false;
};
