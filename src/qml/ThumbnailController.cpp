#include "ThumbnailController.h"

#include "app/Logging.h"
#include "GuiThread.h"
#include "NotificationController.h"

#include <QDebug>
#include <QDir>
#include <QStandardPaths>

ThumbnailController::ThumbnailController(std::shared_ptr<ThumbnailService> service,
                                         std::shared_ptr<FileListModel> model,
                                         NotificationController* notifications,
                                         QObject* parent)
    : QObject(parent), mService(std::move(service)), mModel(std::move(model)),
      mNotifications(notifications)
{}

void ThumbnailController::requestThumbnail(quint64 handle)
{
    // No hasThumbnail/isFolder re-check: this method has a handle, not a row to check
    // flags against, and a stale handle already fails cleanly through getThumbnail's
    // own node lookup, landing in the warning below like any other fetch failure.
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
                                                result.errorCode,
                                                QString::fromStdString(result.errorMessage));
                    return;
                }
                mModel->setThumbnailPath(handle, QString::fromStdString(result.value()));
            });
        });
}

QString ThumbnailController::computeDestinationPath(quint64 handle) const
{
    // One file per handle, so repeated requests overwrite the same cache slot rather
    // than accumulating files.
    QString dir =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/MegaExplorerThumbnails";
    QDir().mkpath(dir);
    // Same native-separator requirement as DownloadController's destination path.
    return QDir::toNativeSeparators(dir + "/" + QString::number(handle) + ".jpg");
}
