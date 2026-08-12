#include "UploadController.h"

#include "app/Logging.h"
#include "core/FileOperationService.h"
#include "core/MegaErrorCodes.h"
#include "GuiThread.h"
#include "NotificationController.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace
{

// A Windows .lnk answers isDir() for whatever it points at, but the drop carries
// the .lnk path and the SDK uploads it as the small file it actually is. Every
// folder/file decision below has to agree with the SDK, so they all come here.
bool isFolderUpload(const QFileInfo& info)
{
    return info.isDir() && !info.isShortcut();
}

} // namespace

UploadController::UploadController(std::shared_ptr<UploadService> service,
                                   std::shared_ptr<FileOperationService> fileOperations,
                                   NotificationController* notifications,
                                   QObject* parent)
    : QObject(parent), mService(std::move(service)), mFileOps(std::move(fileOperations)),
      mNotifications(notifications)
{
    mService->setOnProgress([this](UploadJob) {
        invokeOnGuiThread(this, [this] {
            refreshActiveJob();
        });
    });
    mService->setOnJobFinished([this](UploadJob job) {
        invokeOnGuiThread(this, [this, job = std::move(job)]() mutable {
            --mBatch.pendingJobs;
            const Destination destination{static_cast<quint64>(job.parentHandle), job.parentIsRoot};
            bool replaceStarted = false;
            if (job.state == UploadState::Completed)
            {
                ++mBatch.succeeded;
                mBatch.destinations.insert(destination);

                if (job.replaceHandle != 0)
                {
                    replaceStarted = true;
                    // MEGA has no native overwrite, so "replace" is upload
                    // first, then bin the old node -- never the reverse, which
                    // would lose data if the transfer failed. The batch flush
                    // waits for this so a refreshed listing can't show both.
                    ++mBatch.pendingReplaces;
                    mFileOps->moveToRubbish(
                        job.replaceHandle, [this, destination](Result<void> result) {
                            invokeOnGuiThread(
                                this, [this, destination, result = std::move(result)] {
                                    --mBatch.pendingReplaces;
                                    releaseDestination(destination);
                                    if (!result.success)
                                    {
                                        ++mBatch.replaceFailed;
                                        qCWarning(lcUpload)
                                            << "failed to remove the replaced file:"
                                            << QString::fromStdString(result.errorMessage)
                                            << "code=" << result.errorCode;
                                    }
                                    flushBatchIfDone();
                                });
                        });
                }
            }
            else
            {
                ++mBatch.failed;
                if (job.errorCode == MegaErrorCode::kENoEnt)
                    ++mBatch.destinationGone;
                // Log only -- the batch notification below is this feature's
                // single user-facing report (same rule as DownloadController).
                qCWarning(lcUpload)
                    << "upload failed for" << QString::fromStdString(job.name) << ":"
                    << QString::fromStdString(job.errorMessage) << "code=" << job.errorCode;
            }

            if (!replaceStarted)
                releaseDestination(destination);

            refreshActiveJob(); // reflect whatever's now at the front (or nothing)
            flushBatchIfDone();
        });
    });
}

UploadController::~UploadController()
{
    // Same contract as DownloadController's destructor: clearing the observers stops
    // only deliveries that start after this line, and what makes the rest safe is
    // client->shutdown() joining the SDK thread while both controllers are still
    // alive. The one-shot moveToRubbish callback below can't be unregistered at all
    // and rests on that same stop point.
    mService->setOnProgress(nullptr);
    mService->setOnJobFinished(nullptr);
}

bool UploadController::uploadActive() const
{
    return mActiveJob.has_value();
}

QString UploadController::activeFileName() const
{
    return mActiveJob ? QString::fromStdString(mActiveJob->name) : QString();
}

qreal UploadController::activeProgress() const
{
    if (!mActiveJob || mActiveJob->totalBytes == 0)
        return 0.0;
    return static_cast<qreal>(mActiveJob->transferredBytes) /
           static_cast<qreal>(mActiveJob->totalBytes);
}

int UploadController::pendingCount() const
{
    return static_cast<int>(mService->queueLength());
}

int UploadController::maxFilesPerUpload() const
{
    return kMaxFilesPerUpload;
}

bool UploadController::isUploadingTo(quint64 handle, bool isRoot) const
{
    return mBatch.pendingByDestination.find(Destination{handle, isRoot}) !=
           mBatch.pendingByDestination.end();
}

void UploadController::retainDestination(const Destination& destination, int count)
{
    if (count <= 0)
        return;
    const bool wasIdle =
        mBatch.pendingByDestination.find(destination) == mBatch.pendingByDestination.end();
    mBatch.pendingByDestination[destination] += count;
    if (wasIdle)
        emit activeDestinationsChanged();
}

void UploadController::releaseDestination(const Destination& destination)
{
    auto it = mBatch.pendingByDestination.find(destination);
    if (it == mBatch.pendingByDestination.end())
        return;
    if (--it->second > 0)
        return;
    mBatch.pendingByDestination.erase(it);
    emit activeDestinationsChanged();
}

bool UploadController::canUploadTo(quint64 target, bool targetIsRoot) const
{
    return mService->canUploadTo(static_cast<std::uint64_t>(target), targetIsRoot).success;
}

void UploadController::dropUrls(const QList<QUrl>& urls, quint64 target, bool targetIsRoot)
{
    QStringList paths;
    for (const QUrl& url : urls)
    {
        if (!url.isLocalFile())
            continue; // e.g. an image dragged straight out of a browser
        QFileInfo info(url.toLocalFile());
        if (!info.exists()) // a broken shortcut, or something already deleted
            continue;
        // The path crosses into the SDK's own LocalPath, which splits on '\'
        // on Windows -- same reason DownloadController::computeDestinationPath
        // converts.
        paths.append(QDir::toNativeSeparators(info.absoluteFilePath()));
    }

    if (paths.isEmpty())
    {
        mNotifications->notifyError(QStringLiteral("uploadNothingToUpload"));
        return;
    }

    uploadFiles(paths, target, targetIsRoot);
}

int UploadController::expandedFileCount(const QStringList& localPaths) const
{
    int count = 0;
    for (const QString& path : localPaths)
    {
        QFileInfo info(path);
        if (!isFolderUpload(info))
        {
            ++count;
        }
        else
        {
            // Hidden files are included because startUpload sends them; the
            // count has to match what actually goes up, not what Explorer shows.
            QDirIterator walker(
                path, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
            while (walker.hasNext() && count <= kMaxFilesPerUpload)
            {
                walker.next();
                ++count;
            }
        }
        if (count > kMaxFilesPerUpload)
            break;
    }
    return count;
}

void UploadController::uploadFiles(const QStringList& localPaths, quint64 target, bool targetIsRoot)
{
    if (localPaths.isEmpty())
        return;

    const int fileCount = expandedFileCount(localPaths);
    if (fileCount > kMaxFilesPerUpload)
    {
        mNotifications->notifyError(QStringLiteral("uploadTooManyFiles"));
        return;
    }

    // A single file is what dragging one item onto a folder means, so confirming it
    // would put a click in front of every upload.
    if (fileCount > 1)
    {
        emit uploadRequiresConfirmation(localPaths, fileCount, target, targetIsRoot);
        return;
    }

    askAboutConflicts(localPaths, target, targetIsRoot);
}

void UploadController::uploadConfirmed(const QStringList& localPaths,
                                       quint64 target,
                                       bool targetIsRoot)
{
    if (localPaths.isEmpty())
        return;

    if (expandedFileCount(localPaths) > kMaxFilesPerUpload)
    {
        mNotifications->notifyError(QStringLiteral("uploadTooManyFiles"));
        return;
    }

    askAboutConflicts(localPaths, target, targetIsRoot);
}

void UploadController::askAboutConflicts(const QStringList& localPaths,
                                         quint64 target,
                                         bool targetIsRoot)
{
    std::map<QString, quint64> hits = collisionsFor(localPaths, target, targetIsRoot);
    if (hits.empty())
    {
        enqueueAll(localPaths, {}, target, targetIsRoot);
        return;
    }

    QStringList conflictNames;
    for (const auto& hit : hits)
        conflictNames.append(hit.first);
    emit nameConflictRequiresConfirmation(localPaths, conflictNames, target, targetIsRoot);
}

void UploadController::uploadReplacingExisting(const QStringList& localPaths,
                                               quint64 target,
                                               bool targetIsRoot)
{
    enqueueAll(localPaths, collisionsFor(localPaths, target, targetIsRoot), target, targetIsRoot);
}

void UploadController::uploadSkippingExisting(const QStringList& localPaths,
                                              quint64 target,
                                              bool targetIsRoot)
{
    std::map<QString, quint64> hits = collisionsFor(localPaths, target, targetIsRoot);
    QStringList remaining;
    for (const QString& path : localPaths)
    {
        QFileInfo info(path);
        // A folder was never part of the question -- collisionsFor leaves them
        // out -- so a same-named file's hit must not take it out too.
        if (isFolderUpload(info) || hits.find(info.fileName()) == hits.end())
            remaining.append(path);
    }
    // Nothing left is the user's own choice, so say nothing.
    if (!remaining.isEmpty())
        enqueueAll(remaining, {}, target, targetIsRoot);
}

std::map<QString, quint64> UploadController::collisionsFor(const QStringList& localPaths,
                                                           quint64 target,
                                                           bool targetIsRoot) const
{
    std::vector<std::string> names;
    names.reserve(static_cast<std::size_t>(localPaths.size()));
    for (const QString& path : localPaths)
    {
        QFileInfo info(path);
        if (!isFolderUpload(info))
            names.push_back(info.fileName().toStdString());
    }
    if (names.empty())
        return {};

    Result<std::vector<FileEntry>> result =
        mService->findNameCollisions(static_cast<std::uint64_t>(target), targetIsRoot, names);
    std::map<QString, quint64> hits;
    if (!result.success)
    {
        // Can't ask the question, so don't -- just upload. MEGA stacks a
        // same-name upload as a new version, so this is closer to a silent
        // overwrite than to a clean add; log it.
        qCWarning(lcUpload) << "name-collision check skipped:"
                            << QString::fromStdString(result.errorMessage)
                            << "code=" << result.errorCode;
        return hits;
    }

    for (const FileEntry& entry : result.value())
        hits.emplace(QString::fromStdString(entry.name), static_cast<quint64>(entry.handle));
    return hits;
}

void UploadController::enqueueAll(const QStringList& localPaths,
                                  const std::map<QString, quint64>& replaceHandleByName,
                                  quint64 target,
                                  bool targetIsRoot)
{
    // Two dropped files can share a leaf name (C:\a\x.txt and C:\b\x.txt) while
    // there is only ever one node to replace -- give it to the first of them,
    // or the second Rubbish-bin move would just fail with kENoEnt.
    std::set<quint64> claimedReplaceHandles;
    mBatch.pendingJobs += static_cast<int>(localPaths.size());
    retainDestination(Destination{target, targetIsRoot}, static_cast<int>(localPaths.size()));
    for (const QString& path : localPaths)
    {
        QFileInfo info(path);
        QString name = info.fileName();
        const bool isFolder = isFolderUpload(info);
        quint64 replaceHandle = 0;
        auto it = replaceHandleByName.find(name);
        if (!isFolder && it != replaceHandleByName.end()
            && claimedReplaceHandles.insert(it->second).second)
            replaceHandle = it->second;

        // A folder has no meaningful size of its own, and 0 is what totalBytes
        // already means "not known yet" -- the SDK overwrites it once the
        // recursive transfer reports.
        mService->enqueue(path.toStdString(),
                          name.toStdString(),
                          static_cast<std::uint64_t>(target),
                          targetIsRoot,
                          isFolder ? 0u : static_cast<std::uint64_t>(info.size()),
                          static_cast<std::uint64_t>(replaceHandle));
    }
    refreshActiveJob(); // already on the GUI thread here (called from QML)
}

void UploadController::refreshActiveJob()
{
    mActiveJob = mService->currentJob();
    emit uploadActiveChanged();
}

void UploadController::flushBatchIfDone()
{
    if (mBatch.pendingJobs > 0 || mBatch.pendingReplaces > 0)
        return;
    if (mBatch.succeeded == 0 && mBatch.failed == 0)
        return;

    for (const Destination& destination : mBatch.destinations)
        emit destinationChanged(destination.first, destination.second);

    if (mBatch.succeeded == 0 && mBatch.destinationGone == mBatch.failed)
        mNotifications->notifyOperation(QStringLiteral("uploadDestinationGone"), 0, mBatch.failed);
    else
        mNotifications->notifyOperation(QStringLiteral("upload"), mBatch.succeeded, mBatch.failed);

    if (mBatch.replaceFailed > 0)
    {
        // The uploads themselves succeeded and the new files are there; what
        // needs explaining is that the old ones didn't go away.
        mNotifications->notifyError(QStringLiteral("uploadReplaceFailed"));
    }

    mBatch = Batch{};
}
