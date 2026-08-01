#include "UploadController.h"

#include "app/Logging.h"
#include "core/FileOperationService.h"
#include "core/MegaErrorCodes.h"
#include "NotificationController.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>

namespace
{

// UploadService's callbacks may fire on an SDK-internal background thread
// (see IMegaClient.h), so touching QML-facing state from there has to hop
// onto the GUI thread first. Same file-local duplicate as
// DownloadController's, per that existing precedent.
void invokeOnGuiThread(std::function<void()> fn)
{
    QMetaObject::invokeMethod(qApp, std::move(fn), Qt::QueuedConnection);
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
        invokeOnGuiThread([this] {
            refreshActiveJob();
        });
    });
    mService->setOnJobFinished([this](UploadJob job) {
        invokeOnGuiThread([this, job = std::move(job)]() mutable {
            --mBatch.pendingJobs;
            if (job.state == UploadState::Completed)
            {
                ++mBatch.succeeded;
                mBatch.destinations.insert(
                    Destination{static_cast<quint64>(job.parentHandle), job.parentIsRoot});

                if (job.replaceHandle != 0)
                {
                    // MEGA has no native overwrite, so "replace" is upload
                    // first, then bin the old node -- never the reverse, which
                    // would lose data if the transfer failed. The batch flush
                    // waits for this so a refreshed listing can't show both.
                    ++mBatch.pendingReplaces;
                    mFileOps->moveToRubbish(job.replaceHandle, [this](Result<void> result) {
                        invokeOnGuiThread([this, result = std::move(result)] {
                            --mBatch.pendingReplaces;
                            if (!result.success)
                            {
                                ++mBatch.replaceFailed;
                                qCWarning(lcUpload) << "failed to remove the replaced file:"
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

            refreshActiveJob(); // reflect whatever's now at the front (or nothing)
            flushBatchIfDone();
        });
    });
}

bool UploadController::uploadActive() const
{
    return mHasActiveJob;
}

QString UploadController::activeFileName() const
{
    return QString::fromStdString(mActiveJob.name);
}

qreal UploadController::activeProgress() const
{
    if (!mHasActiveJob || mActiveJob.totalBytes == 0)
        return 0.0;
    return static_cast<qreal>(mActiveJob.transferredBytes) /
           static_cast<qreal>(mActiveJob.totalBytes);
}

int UploadController::pendingCount() const
{
    return static_cast<int>(mService->queueLength());
}

bool UploadController::canUploadTo(quint64 target, bool targetIsRoot) const
{
    return mService->canUploadTo(static_cast<std::uint64_t>(target), targetIsRoot).success;
}

void UploadController::dropUrls(const QList<QUrl>& urls, quint64 target, bool targetIsRoot)
{
    QStringList files;
    int folderCount = 0;
    for (const QUrl& url : urls)
    {
        if (!url.isLocalFile())
            continue; // e.g. an image dragged straight out of a browser
        QFileInfo info(url.toLocalFile());
        if (info.isDir())
        {
            ++folderCount;
            continue;
        }
        if (!info.isFile())
            continue;
        // The path crosses into the SDK's own LocalPath, which splits on '\'
        // on Windows -- same reason DownloadController::computeDestinationPath
        // converts.
        files.append(QDir::toNativeSeparators(info.absoluteFilePath()));
    }

    // Checked before the folder question on purpose: "upload 0 files?" is a
    // terrible thing to ask.
    if (files.isEmpty())
    {
        mNotifications->notifyError(QStringLiteral("uploadNothingToUpload"), QString());
        return;
    }

    if (folderCount > 0)
    {
        emit folderDropRequiresConfirmation(files, folderCount, target, targetIsRoot);
        return;
    }

    uploadFiles(files, target, targetIsRoot);
}

void UploadController::uploadFiles(const QStringList& localPaths, quint64 target, bool targetIsRoot)
{
    if (localPaths.isEmpty())
        return;

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
        if (hits.find(QFileInfo(path).fileName()) == hits.end())
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
        names.push_back(QFileInfo(path).fileName().toStdString());

    Result<std::vector<FileEntry>> result =
        mService->findNameCollisions(static_cast<std::uint64_t>(target), targetIsRoot, names);
    std::map<QString, quint64> hits;
    if (!result.success)
        return hits; // can't ask the question, so don't -- just upload

    for (const FileEntry& entry : result.value)
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
    for (const QString& path : localPaths)
    {
        QFileInfo info(path);
        QString name = info.fileName();
        quint64 replaceHandle = 0;
        auto it = replaceHandleByName.find(name);
        if (it != replaceHandleByName.end() && claimedReplaceHandles.insert(it->second).second)
            replaceHandle = it->second;

        mService->enqueue(path.toStdString(),
                          name.toStdString(),
                          static_cast<std::uint64_t>(target),
                          targetIsRoot,
                          static_cast<std::uint64_t>(info.size()),
                          static_cast<std::uint64_t>(replaceHandle));
    }
    refreshActiveJob(); // already on the GUI thread here (called from QML)
}

void UploadController::refreshActiveJob()
{
    mHasActiveJob = mService->hasCurrentJob();
    if (mHasActiveJob)
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
        mNotifications->notifyError(QStringLiteral("uploadReplaceFailed"), QString());
    }

    mBatch = Batch{};
}
