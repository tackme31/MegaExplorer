#pragma once
#include "IMegaClient.h"

#include <memory>
#include <mutex>
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

// Serializes downloads one at a time over IMegaClient::download: enqueue()
// starts immediately if nothing else is active, otherwise the job waits and
// is auto-started once every job ahead of it in the queue finishes
// (succeeds or fails). SDK-free by construction (depends only on
// IMegaClient), unit-testable with a mocked IMegaClient like the other core
// services.
//
// Unlike FolderNavigationService/SearchService, this service's state can
// genuinely be touched from two threads at once: enqueue() is expected to
// be called from the GUI thread (via DownloadController), while
// IMegaClient::download's onProgress/onDone callbacks may fire from an
// SDK-internal background thread (see IMegaClient.h) while a second
// enqueue() is in flight. mMutex protects mQueue against that race -- the
// existing services don't need one because MegaSdkClient's
// getChildren/getRootChildren/search are all synchronous today, so there's
// never real concurrent access to their state.
//
// mQueue.front(), if present, is always the currently active (or
// about-to-become-active) job; enqueue() only ever appends to the back.
// Completed/failed jobs are removed right after their onJobFinished
// notification fires -- this exposes a live queue, not a download history
// log.
class DownloadService
{
public:
    explicit DownloadService(std::shared_ptr<IMegaClient> client);

    // Enqueues a download of handle -> destinationPath (destinationPath is
    // an exact local file path, already fully resolved by the caller -- see
    // DownloadController). expectedTotalBytes seeds the new job's
    // totalBytes from already-known metadata (FileEntry::sizeBytes) so a
    // consumer reading currentJob() right after this call still gets a real
    // denominator, before MegaApi's first onTransferUpdate arrives. Starts
    // immediately if the queue was empty, otherwise waits its turn. Returns
    // a job id usable to correlate later notifications (and, in the
    // future, to target a cancel(jobId) call).
    std::uint64_t enqueue(std::uint64_t handle,
                          const std::string& name,
                          const std::string& destinationPath,
                          std::uint64_t expectedTotalBytes);

    bool hasCurrentJob() const;
    DownloadJob currentJob() const; // precondition: hasCurrentJob()

    // Forward-looking: the full live queue (active job first, if any, then
    // not-yet-started jobs in enqueue order). Not consumed by
    // DownloadController in this pass, but exists from the start so a
    // future progress-list UI can enumerate it without any DownloadService
    // API change.
    std::vector<DownloadJob> jobs() const;

    // Cheap membership check for DownloadController::downloadFile's
    // already-queued guard -- jobs() above copies the whole queue, which is
    // O(N) per call and thus O(N^2) when a caller loops it once per handle
    // to bulk-enqueue a multi-selection. Linear scan under the same mutex,
    // no copy.
    bool hasJobForHandle(std::uint64_t handle) const;

    // Observer registration: DownloadService is a persistent, multi-call
    // service (unlike the one-shot request/response services above), so a
    // single Result<T> callback per call isn't enough -- callers need
    // notifications for whichever job happens to be active. A single
    // subscriber is enough today (only DownloadController listens);
    // registering a new callback replaces the previous one.
    void setOnProgress(std::function<void(DownloadJob)> onProgress);
    void setOnJobFinished(std::function<void(DownloadJob)> onJobFinished);

private:
    // Starts mQueue.front() if it's Queued and nothing else is active.
    // Locks/unlocks mMutex internally rather than requiring the caller to
    // hold it: IMegaClient::download() is called with no lock held, since
    // MockMegaClient-based tests invoke onProgress/onDone synchronously
    // (from this very call), and holding mMutex across it would
    // self-deadlock the first such test.
    void startNextIfIdle();

    std::shared_ptr<IMegaClient> mClient;
    mutable std::mutex mMutex;
    std::uint64_t mNextId = 1;
    std::vector<DownloadJob> mQueue;
    std::function<void(DownloadJob)> mOnProgress;
    std::function<void(DownloadJob)> mOnJobFinished;
};
