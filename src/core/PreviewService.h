#pragma once
#include "IMegaClient.h"
#include "Result.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// The code an evicted request finishes with. Positive, so it can never collide with
// a real SDK error -- those are all <= 0 (MegaErrorCodes.h).
constexpr int kPreviewSuperseded = 3;

// Fetches previews for the preview pane. Superficially ThumbnailService's twin, but
// the opposite policy on both axes: no cache (the pane must never show a stale
// document), and latest-wins instead of FIFO.
//
// Concretely, one request is in flight and the waiting slot holds at most one more --
// a new request evicts whatever was waiting and finishes it with kPreviewSuperseded.
// Holding Down across a hundred rows therefore costs two SDK calls, not a hundred.
// An evicted request never started, so it wrote no file and needs no cleanup; a
// *started* one cannot be recalled (getPreview takes no MegaCancelToken), which is
// why the generation check and the temp-file delete live in PreviewController.
//
// Images and text share the one slot deliberately: arrowing from a JPEG to a text
// file must supersede the JPEG, not race it. The two kinds return different payloads,
// so the slot holds a thunk per request rather than a job struct with one of each.
//
// Same cross-thread caveat as ThumbnailService: mMutex guards every member, and the
// client is called with no lock held, since its onDone can run before it returns.
class PreviewService
{
public:
    explicit PreviewService(std::shared_ptr<IMegaClient> client);

    // destinationPath is exact and caller-resolved. onDone may run synchronously,
    // from within this call -- either because the handle is already gone, or because
    // this request evicted a waiting one.
    void request(std::uint64_t handle,
                 const std::string& destinationPath,
                 std::function<void(Result<std::string>)> onDone);

    // Same scheduling, byte payload. maxBytes is the hard stop IMegaClient documents.
    void requestText(std::uint64_t handle,
                     std::uint64_t maxBytes,
                     std::function<void(Result<std::vector<char>>)> onDone);

private:
    struct Pending
    {
        // Issues the client call and arranges for finish() to run when it lands.
        std::function<void()> start;
        // Runs instead, with kPreviewSuperseded, if a newer request arrives first.
        std::function<void()> reportSuperseded;
    };

    void enqueue(std::function<void()> start, std::function<void()> reportSuperseded);

    // Starts the waiting request if nothing is in flight; loops only when one
    // finished inside this very call (mirrors ThumbnailService::startNextIfCapacity,
    // trampoline included).
    void startNextIfIdle();

    void finish(const std::function<void()>& deliver);

    std::shared_ptr<IMegaClient> mClient;
    mutable std::mutex mMutex;
    bool mActive = false;
    std::optional<Pending> mPending;

    // Re-entrancy trampoline, as in ThumbnailService. The reachable case here is a
    // fast cursor move over rows whose handles are already gone.
    bool mAdvancing = false;
    bool mAdvanceRequested = false;
};
