#include "PreviewService.h"

namespace
{
constexpr const char* kSupersededMessage = "Superseded by a newer preview request";

// Same struct, same reasons, as DownloadService.cpp's -- see there, including why the
// ordinary exits clear the flag themselves instead of leaving it to the destructor.
struct AdvancingGuard
{
    std::mutex& mutex;
    bool& flag;
    bool armed = true;

    // mutex must be held.
    void clearHeld()
    {
        flag = false;
        armed = false;
    }

    ~AdvancingGuard()
    {
        if (!armed)
            return;
        std::lock_guard<std::mutex> lock(mutex);
        flag = false;
    }
};
} // namespace

PreviewService::PreviewService(std::shared_ptr<IMegaClient> client) : mClient(std::move(client)) {}

void PreviewService::request(std::uint64_t handle,
                             const std::string& destinationPath,
                             std::function<void(Result<std::string>)> onDone)
{
    enqueue(
        [this, handle, destinationPath, onDone] {
            mClient->getPreview(
                handle, destinationPath, [this, onDone](Result<std::string> result) {
                    finish([&onDone, &result] {
                        onDone(std::move(result));
                    });
                });
        },
        [onDone] {
            onDone(Result<std::string>::fail(kSupersededMessage, kPreviewSuperseded));
        });
}

void PreviewService::requestText(std::uint64_t handle,
                                 std::uint64_t maxBytes,
                                 std::function<void(Result<std::vector<char>>)> onDone)
{
    enqueue(
        [this, handle, maxBytes, onDone] {
            mClient->readFileContent(
                handle, maxBytes, [this, onDone](Result<std::vector<char>> result) {
                    finish([&onDone, &result] {
                        onDone(std::move(result));
                    });
                });
        },
        [onDone] {
            onDone(Result<std::vector<char>>::fail(kSupersededMessage, kPreviewSuperseded));
        });
}

void PreviewService::requestRange(std::uint64_t handle,
                                  std::uint64_t offset,
                                  std::uint64_t length,
                                  std::function<void(Result<std::vector<char>>)> onDone)
{
    enqueue(
        [this, handle, offset, length, onDone] {
            mClient->readFileRange(
                handle, offset, length, [this, onDone](Result<std::vector<char>> result) {
                    finish([&onDone, &result] {
                        onDone(std::move(result));
                    });
                });
        },
        [onDone] {
            onDone(Result<std::vector<char>>::fail(kSupersededMessage, kPreviewSuperseded));
        });
}

void PreviewService::enqueue(std::function<void()> start, std::function<void()> reportSuperseded)
{
    std::function<void()> superseded;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mPending)
            superseded = std::move(mPending->reportSuperseded);
        mPending = Pending{std::move(start), std::move(reportSuperseded)};
    }

    if (superseded)
        superseded();
    startNextIfIdle();
}

void PreviewService::startNextIfIdle()
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mAdvancing)
        {
            mAdvanceRequested = true; // whoever is in the loop below picks it up
            return;
        }
        mAdvancing = true;
    }
    AdvancingGuard advancing = {mMutex, mAdvancing};

    for (;;)
    {
        std::function<void()> start;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mAdvanceRequested = false;
            if (mActive || !mPending)
            {
                advancing.clearHeld();
                return;
            }
            start = std::move(mPending->start);
            mPending.reset();
            mActive = true;
        }

        // A start() that throws never reaches finish(), so the one-deep slot has to be
        // given back here -- otherwise the flag is clear but nothing can ever start.
        try
        {
            start();
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mActive = false;
            throw;
        }

        // Same reasoning as ThumbnailService::startNextIfCapacity: a synchronous
        // failure has already run finish() by now, and its startNextIfIdle() only
        // set the flag, so keep looping here rather than letting it recurse.
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mAdvanceRequested)
        {
            advancing.clearHeld();
            return;
        }
    }
}

void PreviewService::finish(const std::function<void()>& deliver)
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mActive)
            return;
        mActive = false;
    }
    deliver();
    startNextIfIdle(); // auto-advance; mMutex isn't held here
}
