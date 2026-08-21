#pragma once
#include "IMegaClient.h"

#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

enum class UploadState
{
    Queued,
    Active,
    Completed,
    Failed,
    // Same meaning as DownloadState::Cancelled -- see there.
    Cancelled,
};

struct UploadJob
{
    std::uint64_t id = 0; // stable for this UploadService's lifetime
    std::string localPath;
    std::string name; // leaf name, for UI display
    std::uint64_t parentHandle = 0;
    bool parentIsRoot = false;

    std::uint64_t nodeHandle = 0; // handle of the created node, once Completed
    UploadState state = UploadState::Queued;
    std::uint64_t transferredBytes = 0;
    std::uint64_t totalBytes = 0; // seeded from enqueue()'s expectedTotalBytes,
                                  // overwritten once real onProgress data arrives
    std::string errorMessage;     // only meaningful when state == Failed
    int errorCode = 0; // only meaningful when state == Failed; mirrors Result<T>::errorCode
};

// Runs up to kMaxConcurrent uploads over IMegaClient::upload, mirroring
// DownloadService down to the mutex discipline, the mActive/mPending split and the
// job-id matching on callbacks -- see there for why those invariants are
// structural.
//
// Deliberately absent, unlike DownloadService:
//
//  - No duplicate suppression. A drop is a single explicit gesture, MEGA allows
//    same-named siblings, and an uploaded node has no handle until the transfer
//    completes, so there would be nothing to key on.
//  - No local-file existence check: src/core has no filesystem access by design. A
//    file gone by its turn just fails startUpload and takes the failure path.
class UploadService
{
public:
    // Same meaning and same value as DownloadService::kMaxConcurrent, counted
    // separately: the two directions do not share a budget.
    static constexpr std::size_t kMaxConcurrent = 4;

    explicit UploadService(std::shared_ptr<IMegaClient> client);

    // localPath is exact and already resolved. expectedTotalBytes seeds totalBytes so
    // a consumer reading currentJob() right away has a denominator. The returned id
    // is what the service matches its own SDK callbacks against.
    std::uint64_t enqueue(const std::string& localPath,
                          const std::string& name,
                          std::uint64_t parentHandle,
                          bool parentIsRoot,
                          std::uint64_t expectedTotalBytes);

    // A copy under the lock, for the same reason as DownloadService::currentJob().
    std::optional<UploadJob> currentJob() const;
    std::vector<UploadJob> jobs() const;

    // O(1), unlike jobs(): dropping thousands of files makes the footer's "n
    // remaining" label a hot path.
    std::size_t queueLength() const;

    // Mirrors DownloadService::cancelAll() exactly, including the one-onJobFinished-
    // per-dropped-job guarantee UploadController's batch counters depend on.
    void cancelAll();

    // Mirrors DownloadService::cancel() -- see there for the lock discipline the
    // active-job branch needs.
    void cancel(std::uint64_t jobId);

    // Synchronous pre-checks straight through to IMegaClient: a hovering drag needs
    // its answer immediately.
    Result<void> canUploadTo(std::uint64_t parentHandle, bool parentIsRoot) const;
    Result<std::vector<FileEntry>> findNameCollisions(std::uint64_t parentHandle,
                                                      bool parentIsRoot,
                                                      const std::vector<std::string>& names) const;

    // Single-subscriber: registering again replaces the previous callback.
    void setOnProgress(std::function<void(UploadJob)> onProgress);
    void setOnJobFinished(std::function<void(UploadJob)> onJobFinished);

private:
    // Fills every free slot from the front of mPending, then keeps going while jobs
    // keep finishing inside this call. Takes mMutex itself: checkUpload() answers on
    // the spot and upload() can run onDone before returning, so holding the lock
    // across either would self-deadlock.
    void startNextIfIdle();

    // mMutex must be held. Null once the job has finished and been dropped.
    UploadJob* activeJob(std::uint64_t jobId);

    // mMutex must be held. Frees the slot and forgets any cancel asked for it.
    // Invalidates every pointer activeJob() handed out.
    void dropActive(std::uint64_t jobId);

    std::shared_ptr<IMegaClient> mClient;
    mutable std::mutex mMutex;
    std::uint64_t mNextId = 1;
    std::vector<UploadJob> mActive; // in-flight, at most kMaxConcurrent, start order
    std::deque<UploadJob> mPending; // not yet started, in enqueue order
    std::function<void(UploadJob)> mOnProgress;
    std::function<void(UploadJob)> mOnJobFinished;

    // Re-entrancy guard for startNextIfIdle(), identical to DownloadService's -- see
    // there for why the recursion it replaces was a real risk.
    bool mAdvancing = false;

    // Same role as DownloadService::mCancelRequested -- see there. The window it
    // covers is wider on this side, since the blocking checkUpload() sits inside it.
    std::set<std::uint64_t> mCancelRequested;
};
