#include "ThumbnailController.h"

#include "app/Logging.h"
#include "GuiThread.h"
#include "NotificationController.h"

#include <QDebug>

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
    mService->request(
        static_cast<std::uint64_t>(handle),
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
                mModel->setThumbnailPath(handle, modelPathFor(result.value()));
            });
        });
}

void ThumbnailController::discardVisibleThumbnails()
{
    const std::vector<std::uint64_t> handles = mModel->thumbnailHandles();
    if (handles.empty())
        return;
    mService->discard(handles);
    ++mDiscardGeneration;
}

QString ThumbnailController::modelPathFor(const std::string& path) const
{
    QString result = QString::fromStdString(path);
    if (mDiscardGeneration != 0)
    {
        // A query, so QUrl::toLocalFile() still yields the file itself -- it is only
        // the pixmap cache, keyed on the whole URL, that has to see a difference.
        result += QStringLiteral("?v=%1").arg(mDiscardGeneration);
    }
    return result;
}
