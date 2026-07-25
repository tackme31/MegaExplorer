#include "ThumbnailController.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QMetaObject>
#include <QStandardPaths>

namespace
{

// Same idiom as FolderNavigationController/DownloadController's own
// invokeOnGuiThread; duplicated here rather than shared, per that existing
// precedent (trivial, 3-line, stateless helper). ThumbnailService's onDone
// callback may fire on an SDK-internal background thread, so touching
// mModel from there must go through a queued invoke onto the GUI thread.
void invokeOnGuiThread(std::function<void()> fn)
{
    QMetaObject::invokeMethod(qApp, std::move(fn), Qt::QueuedConnection);
}

} // namespace

ThumbnailController::ThumbnailController(std::shared_ptr<ThumbnailService> service,
                                         FileListModel* model,
                                         QObject* parent)
    : QObject(parent), mService(std::move(service)), mModel(model)
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
    mService->request(static_cast<std::uint64_t>(handle),
                      destinationPath.toStdString(),
                      [this, handle](Result<std::string> result) {
                          invokeOnGuiThread([this, handle, result = std::move(result)]() mutable {
                              if (!result.success)
                              {
                                  qWarning() << "thumbnail fetch failed for handle" << handle << ":"
                                             << QString::fromStdString(result.errorMessage)
                                             << "code=" << result.errorCode;
                                  return;
                              }
                              mModel->setThumbnailPath(handle,
                                                       QString::fromStdString(result.value));
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
