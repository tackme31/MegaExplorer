#pragma once
#include "IMegaClient.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Fetches server-side thumbnails via IMegaClient::getThumbnail, adding an
// in-memory cache (handle -> local path) and a bounded-concurrency queue on
// top: DownloadService serializes one transfer at a time because downloads
// are heavy, but thumbnails are small and a grid view can trigger dozens of
// requests at once as it scrolls, so up to maxConcurrent run at a time and
// the rest wait in FIFO order. SDK-free by construction, unit-testable with
// a mocked IMegaClient like the other core services.
//
// request() dedupes by handle: a second request for a handle that's already
// cached, active, or queued attaches its callback to the existing
// cache-entry/job instead of calling IMegaClient::getThumbnail again.
// Failed requests are not cached, so a later request for the same handle
// retries.
//
// Same cross-thread caveat as DownloadService: request() is expected to be
// called from the GUI thread (via ThumbnailController), while
// IMegaClient::getThumbnail's onDone may fire from an SDK-internal
// background thread. mMutex protects all the members below against that
// race; IMegaClient::getThumbnail() itself is called with no lock held, for
// the same self-deadlock-avoidance reason as DownloadService::
// startNextIfIdle (onDone can run before getThumbnail() returns).
class ThumbnailService
{
public:
    explicit ThumbnailService(std::shared_ptr<IMegaClient> client, std::size_t maxConcurrent = 4);

    // Resolves handle's thumbnail to a local file path at destinationPath
    // (exact path, already resolved by the caller -- same division of
    // responsibility as IMegaClient::getThumbnail/download). onDone may be
    // invoked synchronously, from within this call, on a cache hit.
    void request(std::uint64_t handle,
                 const std::string& destinationPath,
                 std::function<void(Result<std::string>)> onDone);

private:
    struct Job
    {
        std::string destinationPath;
        std::vector<std::function<void(Result<std::string>)>> callbacks;
    };

    // Starts the next queued handle if capacity allows. Called once after a
    // new job is queued and once after each job finishes; one slot per turn,
    // and it loops only when a request finished inside this very call
    // (mirrors DownloadService::startNextIfIdle, trampoline included).
    void startNextIfCapacity();

    // Common completion path for a job, whether it started directly from
    // request() or later out of the queue.
    void finishJob(std::uint64_t handle, Result<std::string> result);

    std::shared_ptr<IMegaClient> mClient;
    std::size_t mMaxConcurrent;
    mutable std::mutex mMutex;
    std::unordered_map<std::uint64_t, std::string> mCache; // handle -> local path
    std::unordered_map<std::uint64_t, Job> mJobs;          // handle -> active or queued job
    std::deque<std::uint64_t> mQueue;                      // handles waiting for capacity
    std::size_t mActiveCount = 0;

    // Re-entrancy trampoline for startNextIfCapacity(), identical to
    // DownloadService's -- see its comment. The reachable case here is a fast
    // grid scroll queueing dozens of handles that no longer resolve. Both
    // flags live under mMutex.
    bool mAdvancing = false;
    bool mAdvanceRequested = false;
};
