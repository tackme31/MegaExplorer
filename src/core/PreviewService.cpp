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
                    finish(nullptr, [&onDone, &result] {
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
                    finish(nullptr, [&onDone, &result] {
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
    auto stop = std::make_shared<std::atomic<bool>>(false);
    enqueue(
        [this, handle, offset, length, onDone, stop] {
            auto buffer = std::make_shared<std::vector<char>>();
            mClient->readFileRangeStreamed(
                handle,
                offset,
                length,
                [buffer, stop, length](const char* data, std::size_t size) {
                    if (stop->load() || buffer->size() + size > length)
                        return false;
                    buffer->insert(buffer->end(), data, data + size);
                    return true;
                },
                [this, onDone, buffer, stop](Result<void> result) {
                    finish(stop, [&onDone, &buffer, &result] {
                        onDone(result.success ? Result<std::vector<char>>::ok(std::move(*buffer))
                                              : Result<std::vector<char>>::fail(
                                                    std::move(result.errorMessage),
                                                    result.errorCode));
                    });
                });
        },
        [onDone] {
            onDone(Result<std::vector<char>>::fail(kSupersededMessage, kPreviewSuperseded));
        },
        stop);
}

void PreviewService::cancel()
{
    std::function<void()> reportPending;
    std::function<void()> reportActive;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mPending)
        {
            reportPending = std::move(mPending->reportSuperseded);
            mPending.reset();
        }
        if (mActive && mActiveStop)
        {
            mActiveStop->store(true);
            mActiveStop.reset();
            reportActive = std::move(mActiveReportSuperseded);
            mActiveReportSuperseded = nullptr;
            mActive = false;
        }
    }

    if (reportActive)
        reportActive();
    if (reportPending)
        reportPending();
}

void PreviewService::enqueue(std::function<void()> start,
                             std::function<void()> reportSuperseded,
                             std::shared_ptr<std::atomic<bool>> stop)
{
    std::function<void()> superseded;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mPending)
            superseded = std::move(mPending->reportSuperseded);
        mPending = Pending{std::move(start), std::move(reportSuperseded), std::move(stop)};
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
            if (mPending->stop)
            {
                mActiveStop = std::move(mPending->stop);
                mActiveReportSuperseded = std::move(mPending->reportSuperseded);
            }
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
            mActiveStop.reset();
            mActiveReportSuperseded = nullptr;
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

void PreviewService::finish(const std::shared_ptr<std::atomic<bool>>& stop,
                            const std::function<void()>& deliver)
{
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (!mActive || (stop && stop->load()))
            return;
        mActive = false;
        mActiveStop.reset();
        mActiveReportSuperseded = nullptr;
    }
    deliver();
    startNextIfIdle(); // auto-advance; mMutex isn't held here
}
