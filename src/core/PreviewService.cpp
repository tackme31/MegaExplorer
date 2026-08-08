#include "PreviewService.h"

namespace
{
constexpr const char* kSupersededMessage = "Superseded by a newer preview request";
}

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

    for (;;)
    {
        std::function<void()> start;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mAdvanceRequested = false;
            if (mActive || !mPending)
            {
                mAdvancing = false;
                return;
            }
            start = std::move(mPending->start);
            mPending.reset();
            mActive = true;
        }

        start();

        // Same reasoning as ThumbnailService::startNextIfCapacity: a synchronous
        // failure has already run finish() by now, and its startNextIfIdle() only
        // set the flag, so keep looping here rather than letting it recurse.
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mAdvanceRequested)
        {
            mAdvancing = false;
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
