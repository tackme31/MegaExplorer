#include "DownloadController.h"

#include "app/Logging.h"
#include "GuiThread.h"
#include "NotificationController.h"

#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

DownloadController::DownloadController(std::shared_ptr<DownloadService> service,
                                       NotificationController* notifications,
                                       QObject* parent)
    : QObject(parent), mService(std::move(service)), mNotifications(notifications)
{
    mService->setOnProgress([this](DownloadJob) {
        invokeOnGuiThread(this, [this] {
            refreshActiveJob();
        });
    });
    mService->setOnJobFinished([this](DownloadJob job) {
        invokeOnGuiThread(this, [this, job = std::move(job)]() mutable {
            // On success report the *actual* saved leaf name: it differs from
            // job.name when a collision made the SDK rename the file, and showing
            // the pre-rename name reads as if the existing file was overwritten.
            // A failure has no resolvedLocalPath, so job.name is right there.
            QString displayName =
                job.state == DownloadState::Completed
                    ? QFileInfo(QString::fromStdString(job.resolvedLocalPath)).fileName()
                    : QString::fromStdString(job.name);
            if (job.state == DownloadState::Failed)
            {
                // Log only: downloadFinished already surfaces the failure. This is
                // also the last stop for the SDK's English text -- it never reaches
                // the signal.
                qCWarning(lcDownload)
                    << "download failed for" << displayName << ":"
                    << QString::fromStdString(job.errorMessage) << "code=" << job.errorCode;
            }
            emit downloadFinished(job.state == DownloadState::Completed,
                                  displayName,
                                  QString::fromStdString(job.resolvedLocalPath),
                                  job.alreadyPresent);
            refreshActiveJob(); // reflect whatever's now at the front (or nothing)
        });
    });
}

DownloadController::~DownloadController()
{
    // The observers capture a raw this and stay registered for the service's whole
    // life, which outlives this object. Clearing them here only covers deliveries
    // that *start* after this line -- the service copies the observer under its lock
    // and calls the copy after unlocking, so a copy taken just before would still
    // reach a freed this.
    //
    // What closes that window is the composition root's client->shutdown() before the
    // stack unwinds: it joins the SDK thread, so no delivery can be in flight here.
    // That holds only while this and UploadController are app-lifetime singletons --
    // making either per-tab means the observers must hold a weak_ptr instead.
    mService->setOnProgress(nullptr);
    mService->setOnJobFinished(nullptr);
}

bool DownloadController::downloadActive() const
{
    return mActiveJob.has_value();
}

QString DownloadController::activeFileName() const
{
    return mActiveJob ? QString::fromStdString(mActiveJob->name) : QString();
}

qreal DownloadController::activeProgress() const
{
    if (!mActiveJob || mActiveJob->totalBytes == 0)
        return 0.0;
    return static_cast<qreal>(mActiveJob->transferredBytes) /
           static_cast<qreal>(mActiveJob->totalBytes);
}

void DownloadController::downloadFile(quint64 handle, QString name, quint64 sizeBytes)
{
    if (mService->hasJobForHandle(static_cast<std::uint64_t>(handle)))
        return; // already queued/active, don't double-enqueue

    QString destinationPath = computeDestinationPath(name);
    mService->enqueue(static_cast<std::uint64_t>(handle),
                      name.toStdString(),
                      destinationPath.toStdString(),
                      static_cast<std::uint64_t>(sizeBytes));
    refreshActiveJob(); // already on the GUI thread here (called from QML)
}

void DownloadController::openFile(QString localPath)
{
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(localPath)))
    {
        // Log, not toast: openUrl gives no reason to report, and naming the file the
        // user just opened back at them adds nothing.
        qCWarning(lcDownload) << "failed to open downloaded file:" << localPath;
        mNotifications->notifyError(QStringLiteral("openFile"));
    }
}

QString DownloadController::computeDestinationPath(const QString& fileName) const
{
    // Straight into the user's real Downloads folder, no app-specific subfolder, as
    // browsers do. Repeated downloads collide under MegaSdkClient's
    // COLLISION_RESOLUTION_NEW_WITH_N, which suffixes rather than overwrites.
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QDir().mkpath(dir); // defensive: create it if this platform doesn't already have one
    // Native '\' separators: the SDK's LocalPath splits leafName() on '\'
    // specifically, so a '/'-separated path looks like one bare leaf name (drive
    // letter included) and trips an internal invariant assert. Qt's own APIs don't
    // care, but this string crosses into non-Qt code.
    //
    // fileName is a MEGA node name -- server-side data nothing upstream validates,
    // since E2E encryption rules out server-side checks and the SDK only escapes
    // customName, which MegaSdkClient passes as nullptr. This concatenation is the
    // trust boundary, and safeLocalFileName keeps the result a leaf inside dir.
    const QString leaf =
        QString::fromStdString(DownloadService::safeLocalFileName(fileName.toStdString()));
    return QDir::toNativeSeparators(dir + "/" + leaf);
}

void DownloadController::refreshActiveJob()
{
    mActiveJob = mService->currentJob();
    emit downloadActiveChanged();
}
