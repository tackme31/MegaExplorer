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
// SDK-internal background thread (delivery mode 1 in IMegaClient.h) while a
// second enqueue() is in flight. mMutex protects mQueue against that race.
//
// Two invariants make that safe, and both are enforced structurally rather
// than by convention:
//
//  - There is at most one active job, because mActive is an optional rather
//    than a position in a container. mPending holds only not-yet-started
//    jobs, so no code can mistake a waiting job for the running one.
//  - A callback only ever writes to the job it belongs to: the job id is
//    captured by value when the transfer starts and re-checked against
//    mActive->id on arrival, so a callback for an already-finished job is
//    dropped instead of landing on whichever job happens to be active now.
//
// The second one is what a future cancel(jobId) needs: cancelling the active
// job lets the SDK deliver a late onProgress/onDone afterwards, and without
// the id check that would corrupt the job promoted in its place. See
// ThumbnailService, which gets the same property for free by keying jobs on
// the node handle.
//
// Completed/failed jobs are dropped right after their onJobFinished
// notification fires -- this exposes a live queue, not a download history
// log.
class DownloadService
{
public:
    explicit DownloadService(std::shared_ptr<IMegaClient> client);

    // Turns a MEGA node name into a name that can only ever be a leaf inside
    // the caller's chosen directory. Node names are server-side data and MEGA
    // is end-to-end encrypted, so nothing upstream validates them: a name of
    // "..\\..\\evil.exe" would otherwise escape the download directory once
    // concatenated. Static and here rather than in DownloadController because
    // the rule is a Qt-free string transformation, and src/core is where those
    // belong -- see docs/ARCHITECTURE.md's trust-boundary section.
    // No length cap on purpose -- truncating invents collisions, and an
    // over-long path is an OS-level error, not a trust-boundary break.
    static std::string safeLocalFileName(const std::string& nodeName);

    // Enqueues a download of handle -> destinationPath (destinationPath is
    // an exact local file path, already fully resolved by the caller -- see
    // DownloadController, whose leaf name has been through
    // safeLocalFileName above). expectedTotalBytes seeds the new job's
    // totalBytes from already-known metadata (FileEntry::sizeBytes) so a
    // consumer reading currentJob() right after this call still gets a real
    // denominator, before MegaApi's first onTransferUpdate arrives. Starts
    // immediately if the queue was empty, otherwise waits its turn. Returns
    // a job id usable to correlate later notifications; the same id is what
    // the service matches its own SDK callbacks against, and what a future
    // cancel(jobId) would name.
    std::uint64_t enqueue(std::uint64_t handle,
                          const std::string& name,
                          const std::string& destinationPath,
                          std::uint64_t expectedTotalBytes);

    // Snapshot of the active job, or nullopt if nothing is running. Copies
    // under the lock rather than handing out a reference: the completion
    // callback runs on an SDK thread and can clear mActive at any moment.
    std::optional<DownloadJob> currentJob() const;

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
    // Promotes mPending.front() into mActive if nothing else is running, then
    // keeps going for as long as jobs keep finishing inside this call.
    //
    // Locks/unlocks mMutex internally rather than requiring the caller to hold
    // it: IMegaClient::download() may run onProgress/onDone before it returns
    // (IMegaClient.h's delivery mode 3 -- a handle that no longer resolves
    // fails on the calling thread, not from the SDK's), so holding mMutex
    // across it would self-deadlock.
    void startNextIfIdle();

    std::shared_ptr<IMegaClient> mClient;
    mutable std::mutex mMutex;
    std::uint64_t mNextId = 1;
    std::optional<DownloadJob> mActive;  // the one in-flight job, if any
    std::deque<DownloadJob> mPending;    // not yet started, in enqueue order
    std::function<void(DownloadJob)> mOnProgress;
    std::function<void(DownloadJob)> mOnJobFinished;

    // Re-entrancy trampoline for startNextIfIdle(). That same in-stack failure
    // means onDone's auto-advance would otherwise nest one frame per queued
    // job -- 200 selected files against a folder deleted from another client
    // is 200 frames, laid down by the SDK thread whose stack size this app
    // doesn't control. A nested call now just asks the frame already looping
    // to go round again. Both flags live under mMutex.
    bool mAdvancing = false;
    bool mAdvanceRequested = false;
};
