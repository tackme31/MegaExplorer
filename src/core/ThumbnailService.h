#pragma once
#include "ILocalFileSystem.h"
#include "IMegaClient.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// Fetches server-side thumbnails, adding an in-memory handle -> path cache and a
// bounded-concurrency queue: thumbnails are small and a scrolling grid triggers
// dozens at once, so up to maxConcurrent run and the rest wait FIFO. The limit is a
// constructor argument here, unlike DownloadService's fixed one, because a caller
// tunes it per grid.
//
// request() dedupes by handle -- a second request for a cached, active or queued
// handle attaches its callback to the existing entry. Failures are not cached, so a
// later request retries.
//
// The cache outlives the process: the file a previous run left at the handle's path
// is served as a hit, so a handle is fetched once and never again. That is sound
// without any invalidation because MEGA gives changed content a new handle
// (STUDY_THUMBNAIL_CACHE.md 3-3).
//
// Same cross-thread caveat as DownloadService: mMutex guards every member, and
// getThumbnail() is called with no lock held, since its onDone can run before it
// returns.
class ThumbnailService
{
public:
    // cacheDirectory is the root of the on-disk cache; the per-account directory
    // under it is created on demand. Injected as a string because src/core links no
    // Qt and cannot ask QStandardPaths itself.
    ThumbnailService(std::shared_ptr<IMegaClient> client,
                     std::shared_ptr<ILocalFileSystem> fileSystem,
                     std::string cacheDirectory,
                     std::size_t maxConcurrent = 4);

    // The destination is resolved here, not by the caller: it names the signed-in
    // account, which only this class can ask for. onDone may run synchronously, from
    // within this call, on a cache hit.
    void request(std::uint64_t handle, std::function<void(Result<std::string>)> onDone);

private:
    struct Job
    {
        std::string destinationPath;
        std::vector<std::function<void(Result<std::string>)>> callbacks;
        // Tells the entry startNextIfCapacity() handed to the SDK apart from one that a
        // completion running inside that same call re-created under the same handle.
        bool started = false;
    };

    // One slot per turn; loops only when a request finished inside this very call
    // (mirrors DownloadService::startNextIfIdle, trampoline included).
    void startNextIfCapacity();

    void finishJob(std::uint64_t handle, Result<std::string> result);

    std::shared_ptr<IMegaClient> mClient;
    std::shared_ptr<ILocalFileSystem> mFileSystem;
    std::string mCacheDirectory;
    std::size_t mMaxConcurrent;
    mutable std::mutex mMutex;
    // The account the entries below were resolved under. Handles are not documented
    // to be unique across accounts, so a cached path must not survive a switch.
    std::string mAccountDirectory;
    std::unordered_map<std::uint64_t, std::string> mCache; // handle -> local path
    std::unordered_map<std::uint64_t, Job> mJobs;          // handle -> active or queued job
    std::deque<std::uint64_t> mQueue;                      // handles waiting for capacity
    std::size_t mActiveCount = 0;

    // Re-entrancy trampoline for startNextIfCapacity(), as in DownloadService. The
    // reachable case here is a fast scroll queueing dozens of dead handles.
    bool mAdvancing = false;
    bool mAdvanceRequested = false;
};
