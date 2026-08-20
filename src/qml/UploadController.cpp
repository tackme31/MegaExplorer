#include "UploadController.h"

#include "app/Logging.h"
#include "core/MegaErrorCodes.h"
#include "GuiThread.h"
#include "NotificationController.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLocale>

namespace
{

// A Windows .lnk answers isDir() for whatever it points at, but the drop carries
// the .lnk path and the SDK uploads it as the small file it actually is. Every
// folder/file decision below has to agree with the SDK, so they all come here.
bool isFolderUpload(const QFileInfo& info)
{
    return info.isDir() && !info.isShortcut();
}

std::vector<std::string> toLocalPaths(const QStringList& paths)
{
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(paths.size()));
    for (const QString& path : paths)
        out.push_back(path.toStdString());
    return out;
}

// How the dialog names one hit: a bare leaf name cannot be told apart from the same
// name elsewhere in the tree, so a nested one is shown as "folder/sub/name", relative
// to the dropped path it came from (spec 3-3).
QString displayPath(const std::string& localPath, const QStringList& roots)
{
    const QString path = QString::fromStdString(localPath);
    for (const QString& root : roots)
    {
        const QString native = QDir::toNativeSeparators(root);
        const QString prefix = native + QDir::separator();
        if (path.startsWith(prefix))
            return QFileInfo(native).fileName() + QLatin1Char('/') +
                   QDir::fromNativeSeparators(path.mid(prefix.size()));
    }
    return QFileInfo(path).fileName();
}

// Empty for zero, so the dialog can drop the parenthetical rather than print
// "(0 B)". Traditional (1024-based) units for the same reason AccountController
// uses them: that is what MEGA itself quotes.
QString sizeText(qint64 bytes)
{
    if (bytes <= 0)
        return QString();
    // c() not system(): the unit word is locale data, and the UI is English-only.
    return QLocale::c().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

} // namespace

UploadController::UploadController(std::shared_ptr<UploadService> service,
                                   std::shared_ptr<UploadScanService> scan,
                                   NotificationController* notifications,
                                   QObject* parent)
    : QObject(parent), mService(std::move(service)), mScan(std::move(scan)),
      mNotifications(notifications)
{
    mService->setOnProgress([this](UploadJob job) {
        invokeOnGuiThread(this, [this, job = std::move(job)]() mutable {
            emit jobChanged(job);
            refreshActiveJob();
        });
    });
    mService->setOnJobFinished([this](UploadJob job) {
        invokeOnGuiThread(this, [this, job = std::move(job)]() mutable {
            emit jobChanged(job);
            publishQueue(); // whatever was promoted in its place is Active now
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

void UploadController::cancelJob(quint64 jobId)
{
    mService->cancel(jobId);
    refreshActiveJob(); // a dropped pending job is gone already; the active one clears later
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

UploadController::UploadVolume UploadController::expandedVolume(const QStringList& localPaths) const
{
    UploadVolume volume;
    for (const QString& path : localPaths)
    {
        QFileInfo info(path);
        if (!isFolderUpload(info))
        {
            ++volume.files;
            volume.bytes += info.size();
        }
        else
        {
            // Hidden files are included because startUpload sends them; the
            // count has to match what actually goes up, not what Explorer shows.
            QDirIterator walker(path, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
            while (walker.hasNext() && volume.files <= kMaxFilesPerUpload)
            {
                walker.next();
                ++volume.files;
                volume.bytes += walker.fileInfo().size();
            }
        }
        if (volume.files > kMaxFilesPerUpload)
            break;
    }
    return volume;
}

void UploadController::uploadFiles(const QStringList& localPaths, quint64 target, bool targetIsRoot)
{
    if (localPaths.isEmpty())
        return;

    const int fileCount = expandedVolume(localPaths).files;
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

    if (expandedVolume(localPaths).files > kMaxFilesPerUpload)
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
    const std::vector<UploadCollision> hits = collisionsFor(localPaths, target, targetIsRoot);
    if (hits.empty())
    {
        enqueueAll(localPaths, target, targetIsRoot);
        return;
    }

    QStringList conflictNames;
    conflictNames.reserve(static_cast<qsizetype>(hits.size()));
    qint64 collidedBytes = 0;
    for (const UploadCollision& hit : hits)
    {
        conflictNames.append(displayPath(hit.localPath, localPaths));
        collidedBytes += QFileInfo(QString::fromStdString(hit.localPath)).size();
    }
    // Counted rather than taken from the scan: a folder that collides by name only
    // is not a hit, but everything inside it still goes up.
    const UploadVolume total = expandedVolume(localPaths);
    const int collided = static_cast<int>(hits.size());
    const int unaffected = total.files > collided ? total.files - collided : 0;
    const qint64 unaffectedBytes =
        total.bytes > collidedBytes ? total.bytes - collidedBytes : qint64{0};
    emit nameConflictRequiresConfirmation(localPaths,
                                          conflictNames,
                                          unaffected,
                                          sizeText(collidedBytes),
                                          sizeText(unaffectedBytes),
                                          target,
                                          targetIsRoot);
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
    Result<std::vector<UploadPlanItem>> plan =
        mScan->planSkippingCollisions(toLocalPaths(localPaths),
                                      static_cast<std::uint64_t>(target),
                                      targetIsRoot);
    if (!plan.success)
    {
        // Falling back to a plain upload would version over the very files the user
        // just asked to leave alone, so an unanswerable scan stops the operation
        // instead -- the opposite call from collisionsFor(), where the same failure
        // only costs a question.
        qCWarning(lcUpload) << "skip plan failed:" << QString::fromStdString(plan.errorMessage)
                            << "code=" << plan.errorCode;
        mNotifications->notifyError(QStringLiteral("uploadSkipFailed"));
        return;
    }
    // Nothing left is the user's own choice, so say nothing.
    enqueuePlan(plan.value());
}

std::vector<UploadCollision> UploadController::collisionsFor(const QStringList& localPaths,
                                                             quint64 target,
                                                             bool targetIsRoot) const
{
    Result<std::vector<UploadCollision>> result =
        mScan->findCollisions(toLocalPaths(localPaths),
                              static_cast<std::uint64_t>(target),
                              targetIsRoot);
    if (!result.success)
    {
        // Can't ask the question, so don't -- just upload. MEGA stacks a
        // same-name upload as a new version, so this is closer to a silent
        // overwrite than to a clean add; log it.
        qCWarning(lcUpload) << "name-collision check skipped:"
                            << QString::fromStdString(result.errorMessage)
                            << "code=" << result.errorCode;
        return {};
    }
    return result.value();
}

void UploadController::enqueueAll(const QStringList& localPaths,
                                  quint64 target,
                                  bool targetIsRoot)
{
    std::vector<UploadPlanItem> plan;
    plan.reserve(static_cast<std::size_t>(localPaths.size()));
    for (const QString& path : localPaths)
        plan.push_back(UploadPlanItem{path.toStdString(),
                                      static_cast<std::uint64_t>(target),
                                      targetIsRoot});
    enqueuePlan(plan);
}

void UploadController::enqueuePlan(const std::vector<UploadPlanItem>& plan)
{
    if (plan.empty())
        return;

    mBatch.pendingJobs += static_cast<int>(plan.size());
    for (const UploadPlanItem& item : plan)
    {
        const Destination destination{static_cast<quint64>(item.parentHandle), item.parentIsRoot};
        retainDestination(destination, 1);
        QFileInfo info(QString::fromStdString(item.localPath));
        // A folder has no meaningful size of its own, and 0 is what totalBytes
        // already means "not known yet" -- the SDK overwrites it once the
        // recursive transfer reports.
        mService->enqueue(item.localPath,
                          info.fileName().toStdString(),
                          item.parentHandle,
                          item.parentIsRoot,
                          isFolderUpload(info) ? 0u : static_cast<std::uint64_t>(info.size()));
    }
    publishQueue();
    refreshActiveJob(); // already on the GUI thread here (called from QML)
}

void UploadController::refreshActiveJob()
{
    mActiveJob = mService->currentJob();
    emit uploadActiveChanged();
}

void UploadController::publishQueue()
{
    for (const UploadJob& job : mService->jobs())
        emit jobChanged(job);
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
