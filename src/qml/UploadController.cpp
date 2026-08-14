#include "UploadController.h"

#include "app/Logging.h"
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
                                   NotificationController* notifications,
                                   QObject* parent)
    : QObject(parent), mService(std::move(service)), mNotifications(notifications)
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
            if (job.state == UploadState::Completed)
            {
                ++mBatch.succeeded;
                mBatch.destinations.insert(destination);
            }
            else if (job.state != UploadState::Cancelled)
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
            // A cancelled job is counted as neither, which is what keeps the batch
            // flush silent about it -- cancelUploads() already reported the stop for
            // the whole queue.

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
    // alive.
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

void UploadController::cancelUploads()
{
    const int queued = pendingCount();
    if (queued == 0)
        return;
    mService->cancelAll();
    mNotifications->notifyOperation(QStringLiteral("uploadCancelled"), queued, 0);
    refreshActiveJob(); // pending jobs are gone already; the active one clears later
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
            QDirIterator walker(path, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
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
    const std::set<QString> hits = collisionsFor(localPaths, target, targetIsRoot);
    if (hits.empty())
    {
        enqueueAll(localPaths, target, targetIsRoot);
        return;
    }

    QStringList conflictNames;
    for (const QString& name : hits)
        conflictNames.append(name);
    emit nameConflictRequiresConfirmation(localPaths, conflictNames, target, targetIsRoot);
}

void UploadController::uploadReplacingExisting(const QStringList& localPaths,
                                               quint64 target,
                                               bool targetIsRoot)
{
    // Nothing to arrange: MEGA turns a same-named upload into a new version of the
    // node that is already there, so a replace is just an ordinary upload.
    enqueueAll(localPaths, target, targetIsRoot);
}

void UploadController::uploadSkippingExisting(const QStringList& localPaths,
                                              quint64 target,
                                              bool targetIsRoot)
{
    const std::set<QString> hits = collisionsFor(localPaths, target, targetIsRoot);
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
        enqueueAll(remaining, target, targetIsRoot);
}

std::set<QString> UploadController::collisionsFor(const QStringList& localPaths,
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
    std::set<QString> hits;
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
        hits.insert(QString::fromStdString(entry.name));
    return hits;
}

void UploadController::enqueueAll(const QStringList& localPaths,
                                  quint64 target,
                                  bool targetIsRoot)
{
    mBatch.pendingJobs += static_cast<int>(localPaths.size());
    retainDestination(Destination{target, targetIsRoot}, static_cast<int>(localPaths.size()));
    for (const QString& path : localPaths)
    {
        QFileInfo info(path);
        // A folder has no meaningful size of its own, and 0 is what totalBytes
        // already means "not known yet" -- the SDK overwrites it once the
        // recursive transfer reports.
        mService->enqueue(path.toStdString(),
                          info.fileName().toStdString(),
                          static_cast<std::uint64_t>(target),
                          targetIsRoot,
                          isFolderUpload(info) ? 0u : static_cast<std::uint64_t>(info.size()));
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
    if (mBatch.pendingJobs > 0)
        return;
    if (mBatch.succeeded == 0 && mBatch.failed == 0)
        return;

    for (const Destination& destination : mBatch.destinations)
        emit destinationChanged(destination.first, destination.second);

    if (mBatch.succeeded == 0 && mBatch.destinationGone == mBatch.failed)
        mNotifications->notifyOperation(QStringLiteral("uploadDestinationGone"), 0, mBatch.failed);
    else
        mNotifications->notifyOperation(QStringLiteral("upload"), mBatch.succeeded, mBatch.failed);

    mBatch = Batch{};
}
