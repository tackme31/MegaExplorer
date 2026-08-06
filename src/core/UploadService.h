#pragma once
#include "IMegaClient.h"

#include <memory>
#include <mutex>
#include <optional>
#include <vector>

enum class UploadState
{
    Queued,
    Active,
    Completed,
    Failed,
};

struct UploadJob
{
    std::uint64_t id = 0; // stable for this UploadService's lifetime
    std::string localPath;
    std::string name; // leaf name, for UI display
    std::uint64_t parentHandle = 0;
    bool parentIsRoot = false;

    // Handle of an existing same-named file the user chose to replace, or 0
    // for none. UploadService never interprets this -- it rides along in the
    // job and comes back untouched in the finished-job notification. Moving
    // that old node to the Rubbish bin is UploadController's job (via
    // FileOperationService), so the queue stays the single source of order
    // without this service having to own a two-step upload-then-delete
    // transaction.
    std::uint64_t replaceHandle = 0;

    std::uint64_t nodeHandle = 0; // handle of the created node, once Completed
    UploadState state = UploadState::Queued;
    std::uint64_t transferredBytes = 0;
    std::uint64_t totalBytes = 0; // seeded from enqueue()'s expectedTotalBytes,
                                  // overwritten once real onProgress data arrives
    std::string errorMessage;     // only meaningful when state == Failed
    int errorCode = 0; // only meaningful when state == Failed; mirrors Result<T>::errorCode
};

// Serializes uploads one at a time over IMegaClient::upload, mirroring
// DownloadService: enqueue() starts immediately if nothing else is active,
// otherwise the job waits and is auto-started once every job ahead of it
// finishes (succeeds or fails). Same mutex discipline, same "mQueue.front()
// is always the active job", same "finished jobs are erased right after
// their notification" -- this is a live queue, not an upload history log.
//
// Deliberately absent, unlike DownloadService:
//
//  - No duplicate suppression. DownloadController needs hasJobForHandle
//    because a double-click or a menu item can be fired repeatedly; a drop
//    is a single explicit gesture. MEGA also allows same-named siblings, and
//    an uploaded node has no handle until the transfer completes, so there
//    would be nothing to key on anyway.
//  - No local-file existence check. src/core has no filesystem access by
//    design. If the file is gone by the time its turn comes, startUpload
//    fails and the job takes the ordinary failure path.
class UploadService
{
public:
    explicit UploadService(std::shared_ptr<IMegaClient> client);

    // Enqueues an upload of localPath (an exact, already-resolved local file
    // path) into (parentHandle, parentIsRoot). name is the leaf name shown in
    // the UI; expectedTotalBytes seeds totalBytes from already-known metadata
    // so a consumer reading currentJob() right away still has a denominator.
    // replaceHandle is opaque pass-through data, see UploadJob. Returns a job
    // id usable to correlate later notifications.
    std::uint64_t enqueue(const std::string& localPath,
                          const std::string& name,
                          std::uint64_t parentHandle,
                          bool parentIsRoot,
                          std::uint64_t expectedTotalBytes,
                          std::uint64_t replaceHandle = 0);

    // Snapshot of the active job, or nullopt if the queue is empty. Same
    // single-lock reason as DownloadService::currentJob().
    std::optional<UploadJob> currentJob() const;
    std::vector<UploadJob> jobs() const;

    // O(1), unlike jobs(). Dropping thousands of files makes this the hot
    // path behind the footer's "n remaining" label.
    std::size_t queueLength() const;

    // Synchronous pre-checks, passed straight through to IMegaClient. Same
    // "async execution plus a synchronous can-I" split as
    // FileOperationService::canMove, for the same reason: a hovering drag
    // needs an answer immediately.
    Result<void> canUploadTo(std::uint64_t parentHandle, bool parentIsRoot) const;
    Result<std::vector<FileEntry>> findNameCollisions(std::uint64_t parentHandle,
                                                      bool parentIsRoot,
                                                      const std::vector<std::string>& names) const;

    // Single-subscriber observers, same contract as DownloadService's:
    // registering a new callback replaces the previous one.
    void setOnProgress(std::function<void(UploadJob)> onProgress);
    void setOnJobFinished(std::function<void(UploadJob)> onJobFinished);

private:
    // Starts mQueue.front() if it's Queued and nothing else is active.
    // Locks/unlocks mMutex internally rather than requiring the caller to
    // hold it: IMegaClient::upload() is called with no lock held, since
    // MockMegaClient-based tests invoke onProgress/onDone synchronously
    // (from this very call), and holding mMutex across it would
    // self-deadlock the first such test.
    void startNextIfIdle();

    std::shared_ptr<IMegaClient> mClient;
    mutable std::mutex mMutex;
    std::uint64_t mNextId = 1;
    std::vector<UploadJob> mQueue;
    std::function<void(UploadJob)> mOnProgress;
    std::function<void(UploadJob)> mOnJobFinished;
};
