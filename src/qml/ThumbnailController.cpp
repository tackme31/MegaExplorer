#include "ThumbnailController.h"

#include "app/Logging.h"
#include "GuiThread.h"
#include "NotificationController.h"

#include <QDebug>
#include <QDir>
#include <QStandardPaths>

ThumbnailController::ThumbnailController(std::shared_ptr<ThumbnailService> service,
                                         FileListModel* model,
                                         NotificationController* notifications,
                                         QObject* parent)
    : QObject(parent), mService(std::move(service)), mModel(model), mNotifications(notifications)
{}

void ThumbnailController::requestThumbnail(quint64 handle)
{
    // No hasThumbnail/isFolder re-check here: this method only has a handle,
    // not a row/FileEntry to check flags against, and a stale/unknown handle
    // already fails cleanly through IMegaClient::getThumbnail's own
    // resolveNode() lookup (MegaSdkClient), landing in the qWarning() below
    // like any other fetch failure. The QML caller (not yet wired) is
    // expected to only invoke this for hasThumbnail == true && isFolder ==
    // false rows.
    QString destinationPath = computeDestinationPath(handle);
    mService->request(
        static_cast<std::uint64_t>(handle),
        destinationPath.toStdString(),
        [this, handle, self = shared_from_this()](Result<std::string> result) {
            invokeOnGuiThread(this, [this, handle, result = std::move(result)]() mutable {
                if (!result.success)
                {
                    qCWarning(lcThumbnail) << "thumbnail fetch failed for handle" << handle << ":"
                                           << QString::fromStdString(result.errorMessage)
                                           << "code=" << result.errorCode;
                    mNotifications->notifyError(QStringLiteral("thumbnail"),
                                                QString::fromStdString(result.errorMessage));
                    return;
                }
                mModel->setThumbnailPath(handle, QString::fromStdString(result.value));
            });
        });
}

QString ThumbnailController::computeDestinationPath(quint64 handle) const
{
    // TempLocation + an app-specific subfolder, mirroring
    // DownloadController::computeDestinationPath's DownloadLocation pattern.
    // One file per handle, so repeated requestThumbnail() calls for the same
    // handle overwrite the same cache slot instead of accumulating files --
    // harmless either way since ThumbnailService dedupes concurrent/cached
    // requests before ever reaching IMegaClient::getThumbnail.
    QString dir =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/MegaExplorerThumbnails";
    QDir().mkpath(dir);
    // Same native-separator requirement as DownloadController -- MEGA SDK's
    // localpath.cpp splits on '\' on Windows; see
    // DownloadController::computeDestinationPath for the full explanation.
    return QDir::toNativeSeparators(dir + "/" + QString::number(handle) + ".jpg");
}
