#include "DownloadService.h"

#include <array>

namespace
{

bool isAsciiLetter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool isForbiddenChar(char c)
{
    // Bytes >= 0x80 are left alone so UTF-8 sequences survive intact.
    if (static_cast<unsigned char>(c) < 0x20)
        return true;
    switch (c)
    {
        case '<':
        case '>':
        case ':':
        case '"':
        case '|':
        case '?':
        case '*':
            return true;
        default:
            return false;
    }
}

// Windows claims these whatever the extension, so "CON.txt" is as unusable as
// "CON" -- a usability guard rather than a security one, but it belongs in the
// same single rule.
bool isReservedDeviceName(const std::string& stem)
{
    static const std::array<const char*, 22> reserved = {
        "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
        "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};

    std::string upper = stem;
    for (char& c : upper)
    {
        if (c >= 'a' && c <= 'z')
            c = static_cast<char>(c - 'a' + 'A');
    }
    for (const char* name : reserved)
    {
        if (upper == name)
            return true;
    }
    return false;
}

} // namespace

DownloadService::DownloadService(std::shared_ptr<IMegaClient> client) : mClient(std::move(client))
{}

std::string DownloadService::safeLocalFileName(const std::string& nodeName)
{
    // Everything up to the last separator is a directory the node name has no
    // business choosing -- this is the step that stops "..\..\evil.exe".
    const std::size_t lastSeparator = nodeName.find_last_of("/\\");
    std::string leaf =
        (lastSeparator == std::string::npos) ? nodeName : nodeName.substr(lastSeparator + 1);

    // "C:evil.exe" is drive-relative and has no separator to cut at. Must
    // happen before the ':' below turns into '_'.
    if (leaf.size() >= 2 && isAsciiLetter(leaf[0]) && leaf[1] == ':')
        leaf.erase(0, 2);

    for (char& c : leaf)
    {
        if (isForbiddenChar(c))
            c = '_';
    }

    // Windows silently drops trailing dots and spaces, so a name that relied
    // on them wouldn't round-trip. Also collapses "." / ".." / "..." to empty,
    // which the fallback below then names.
    const std::size_t lastKept = leaf.find_last_not_of(". ");
    leaf = (lastKept == std::string::npos) ? std::string() : leaf.substr(0, lastKept + 1);

    if (leaf.empty())
        return "download";

    if (isReservedDeviceName(leaf.substr(0, leaf.find('.'))))
        leaf.insert(leaf.begin(), '_');

    return leaf;
}

std::uint64_t DownloadService::enqueue(std::uint64_t handle,
                                       const std::string& name,
                                       const std::string& destinationPath,
                                       std::uint64_t expectedTotalBytes)
{
    std::uint64_t id;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        DownloadJob job;
        job.id = mNextId++;
        job.handle = handle;
        job.name = name;
        job.destinationPath = destinationPath;
        job.totalBytes = expectedTotalBytes;
        mQueue.push_back(job);
        id = job.id;
    }
    startNextIfIdle();
    return id;
}

std::optional<DownloadJob> DownloadService::currentJob() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    if (mQueue.empty())
        return std::nullopt;
    return mQueue.front();
}

std::vector<DownloadJob> DownloadService::jobs() const
{
    std::lock_guard<std::mutex> lock(mMutex);
    return mQueue;
}

bool DownloadService::hasJobForHandle(std::uint64_t handle) const
{
    std::lock_guard<std::mutex> lock(mMutex);
    for (const DownloadJob& job : mQueue)
    {
        if (job.handle == handle)
            return true;
    }
    return false;
}

void DownloadService::setOnProgress(std::function<void(DownloadJob)> onProgress)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mOnProgress = std::move(onProgress);
}

void DownloadService::setOnJobFinished(std::function<void(DownloadJob)> onJobFinished)
{
    std::lock_guard<std::mutex> lock(mMutex);
    mOnJobFinished = std::move(onJobFinished);
}

void DownloadService::startNextIfIdle()
{
    std::uint64_t handle;
    std::string destinationPath;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mQueue.empty() || mQueue.front().state != DownloadState::Queued)
            return;
        mQueue.front().state = DownloadState::Active;
        handle = mQueue.front().handle;
        destinationPath = mQueue.front().destinationPath;
    }

    mClient->download(
        handle,
        destinationPath,
        [this](std::uint64_t transferred, std::uint64_t total) {
            std::function<void(DownloadJob)> onProgress;
            DownloadJob snapshot;
            {
                std::lock_guard<std::mutex> lock(mMutex);
                if (mQueue.empty())
                    return;
                mQueue.front().transferredBytes = transferred;
                mQueue.front().totalBytes = total;
                snapshot = mQueue.front();
                onProgress = mOnProgress;
            }
            if (onProgress)
                onProgress(snapshot);
        },
        [this](Result<DownloadOutcome> result) {
            std::function<void(DownloadJob)> onJobFinished;
            DownloadJob snapshot;
            {
                std::lock_guard<std::mutex> lock(mMutex);
                if (mQueue.empty())
                    return;
                mQueue.front().state =
                    result.success ? DownloadState::Completed : DownloadState::Failed;
                if (result.success)
                {
                    mQueue.front().resolvedLocalPath = result.value.localPath;
                    mQueue.front().alreadyPresent = result.value.alreadyPresent;
                }
                else
                {
                    mQueue.front().errorMessage = result.errorMessage;
                    mQueue.front().errorCode = result.errorCode;
                }
                snapshot = mQueue.front();
                onJobFinished = mOnJobFinished;
                mQueue.erase(mQueue.begin());
            }
            if (onJobFinished)
                onJobFinished(snapshot);
            startNextIfIdle(); // auto-advance; mMutex isn't held here
        });
}
