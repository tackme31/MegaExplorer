#include "DownloadController.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
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

DownloadController::DownloadController(std::shared_ptr<DownloadService> service, QObject* parent)
    : QObject(parent), mService(std::move(service))
{
    mService->setOnProgress([this](DownloadJob) {
        invokeOnGuiThread([this] {
            refreshActiveJob();
        });
    });
    mService->setOnJobFinished([this](DownloadJob job) {
        invokeOnGuiThread([this, job = std::move(job)]() mutable {
            emit downloadFinished(job.state == DownloadState::Completed,
                                  QString::fromStdString(job.name),
                                  QString::fromStdString(job.resolvedLocalPath),
                                  QString::fromStdString(job.errorMessage));
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
    for (const DownloadJob& job : mService->jobs())
    {
        if (job.handle == static_cast<std::uint64_t>(handle))
            return; // already queued/active, don't double-enqueue
    }

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
        qWarning() << "failed to open downloaded file:" << localPath;
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
    return QDir::toNativeSeparators(dir + "/" + fileName);
}

void DownloadController::refreshActiveJob()
{
    mHasActiveJob = mService->hasCurrentJob();
    if (mHasActiveJob)
        mActiveJob = mService->currentJob();
    emit downloadActiveChanged();
}
