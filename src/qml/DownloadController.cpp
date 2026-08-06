#include "DownloadController.h"

#include "app/Logging.h"
#include "NotificationController.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QStandardPaths>
#include <QUrl>

namespace
{

// DownloadService's onProgress/onJobFinished callbacks may fire on an
// SDK-internal background thread (see IMegaClient.h), so touching the
// QML-facing properties/signals from there must go through a queued invoke
// onto the GUI thread. Same idiom as FolderNavigationController's own
// invokeOnGuiThread; duplicated here rather than shared, per that existing
// precedent (trivial, 3-line, stateless helper).
void invokeOnGuiThread(std::function<void()> fn)
{
    QMetaObject::invokeMethod(qApp, std::move(fn), Qt::QueuedConnection);
}

} // namespace

DownloadController::DownloadController(std::shared_ptr<DownloadService> service,
                                       NotificationController* notifications,
                                       QObject* parent)
    : QObject(parent), mService(std::move(service)), mNotifications(notifications)
{
    mService->setOnProgress([this](DownloadJob) {
        invokeOnGuiThread([this] {
            refreshActiveJob();
        });
    });
    mService->setOnJobFinished([this](DownloadJob job) {
        invokeOnGuiThread([this, job = std::move(job)]() mutable {
            // On success, report the *actual* saved leaf name (from
            // resolvedLocalPath), not the originally-requested job.name --
            // they differ when a name collision caused the SDK to rename the
            // saved file (see IMegaClient::download), and showing the
            // pre-rename name in the snackbar reads as if the existing file
            // got overwritten even though a distinct "(1)"-suffixed file was
            // actually written. Failure has no resolvedLocalPath, so job.name
            // (what the user asked to download) is still the right thing to
            // show there.
            QString displayName =
                job.state == DownloadState::Completed
                    ? QFileInfo(QString::fromStdString(job.resolvedLocalPath)).fileName()
                    : QString::fromStdString(job.name);
            if (job.state == DownloadState::Failed)
            {
                // Log only -- DownloadSnackbar already surfaces this to the
                // user via downloadFinished below, so no notifyError() here.
                qCWarning(lcDownload)
                    << "download failed for" << displayName << ":"
                    << QString::fromStdString(job.errorMessage) << "code=" << job.errorCode;
            }
            emit downloadFinished(job.state == DownloadState::Completed,
                                  displayName,
                                  QString::fromStdString(job.resolvedLocalPath),
                                  QString::fromStdString(job.errorMessage),
                                  job.alreadyPresent);
            refreshActiveJob(); // reflect whatever's now at the front (or nothing)
        });
    });
}

bool DownloadController::downloadActive() const
{
    return mHasActiveJob;
}

QString DownloadController::activeFileName() const
{
    return QString::fromStdString(mActiveJob.name);
}

qreal DownloadController::activeProgress() const
{
    if (!mHasActiveJob || mActiveJob.totalBytes == 0)
        return 0.0;
    return static_cast<qreal>(mActiveJob.transferredBytes) /
           static_cast<qreal>(mActiveJob.totalBytes);
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
        qCWarning(lcDownload) << "failed to open downloaded file:" << localPath;
        mNotifications->notifyError(QStringLiteral("openFile"), localPath);
    }
}

QString DownloadController::computeDestinationPath(const QString& fileName) const
{
    // QStandardPaths::DownloadLocation resolves to the platform's real
    // user-facing Downloads folder (SHGetKnownFolderPath(FOLDERID_Downloads)
    // on Windows, XDG_DOWNLOAD_DIR on Linux, etc.) -- saving straight into it,
    // no app-specific subfolder, matches ordinary browser download behavior.
    // Name collisions across repeated downloads are handled by MegaSdkClient's
    // COLLISION_RESOLUTION_NEW_WITH_N (numbered suffix, never overwrites).
    QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QDir().mkpath(dir); // defensive: create it if this platform doesn't already have one
    // MegaApi::startDownload hands this straight to the SDK's own LocalPath/
    // Path (see third_party/sdk/src/localpath.cpp), which on Windows expects
    // native '\' separators -- its leafName() splits on '\' specifically, so
    // a '/'-separated path is treated as having no separator at all, making
    // the whole string (including the "C:" drive letter) look like a bare
    // leaf name and tripping an internal invariant assert. Qt's own APIs are
    // fine with '/', but this string crosses into non-Qt code.
    //
    // fileName is a MEGA node name, i.e. server-side data nothing upstream
    // validates (E2E encryption rules out server-side checks, and the SDK only
    // escapes startUpload/startDownload's customName, which MegaSdkClient
    // passes as nullptr). This concatenation is therefore the trust boundary:
    // safeLocalFileName is what keeps the result a leaf inside dir.
    const QString leaf =
        QString::fromStdString(DownloadService::safeLocalFileName(fileName.toStdString()));
    return QDir::toNativeSeparators(dir + "/" + leaf);
}

void DownloadController::refreshActiveJob()
{
    mHasActiveJob = mService->hasCurrentJob();
    if (mHasActiveJob)
        mActiveJob = mService->currentJob();
    emit downloadActiveChanged();
}
