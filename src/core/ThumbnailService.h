#pragma once
#include "IMegaClient.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Fetches server-side thumbnails, adding an in-memory handle -> path cache and a
// bounded-concurrency queue: DownloadService serializes because downloads are
// heavy, but thumbnails are small and a scrolling grid triggers dozens at once, so
// up to maxConcurrent run and the rest wait FIFO.
//
// request() dedupes by handle -- a second request for a cached, active or queued
// handle attaches its callback to the existing entry. Failures are not cached, so a
// later request retries.
//
// Same cross-thread caveat as DownloadService: mMutex guards every member, and
// getThumbnail() is called with no lock held, since its onDone can run before it
// returns.
class ThumbnailService
{
public:
    explicit ThumbnailService(std::shared_ptr<IMegaClient> client, std::size_t maxConcurrent = 4);

    // destinationPath is exact and caller-resolved. onDone may run synchronously,
    // from within this call, on a cache hit.
    void request(std::uint64_t handle,
                 const std::string& destinationPath,
                 std::function<void(Result<std::string>)> onDone);

private:
    struct Job
    {
        std::string destinationPath;
        std::vector<std::function<void(Result<std::string>)>> callbacks;
    };

    // One slot per turn; loops only when a request finished inside this very call
    // (mirrors DownloadService::startNextIfIdle, trampoline included).
    void startNextIfCapacity();

    void finishJob(std::uint64_t handle, Result<std::string> result);

    std::shared_ptr<IMegaClient> mClient;
    std::size_t mMaxConcurrent;
    mutable std::mutex mMutex;
    std::unordered_map<std::uint64_t, std::string> mCache; // handle -> local path
    std::unordered_map<std::uint64_t, Job> mJobs;          // handle -> active or queued job
    std::deque<std::uint64_t> mQueue;                      // handles waiting for capacity
    std::size_t mActiveCount = 0;

    // Re-entrancy trampoline for startNextIfCapacity(), as in DownloadService. The
    // reachable case here is a fast scroll queueing dozens of dead handles.
    bool mAdvancing = false;
    bool mAdvanceRequested = false;
};
