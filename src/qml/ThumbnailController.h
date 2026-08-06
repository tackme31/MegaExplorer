#pragma once
#include "core/ThumbnailService.h"
#include "FileListModel.h"

#include <QObject>
#include <QString>

#include <memory>

class NotificationController;

// QML-facing GUI glue wrapping ThumbnailService, registered per-tab as its
// own "thumbnailController" context property since Phase 9 (previously a
// single app-lifetime instance) -- separate from
// FolderNavigationController/DownloadController -- see
// FolderNavigationController::fileListModelForThumbnails() for why this
// class (unlike DownloadController) is allowed to write directly into the
// FileListModel FolderNavigationController owns: thumbnails have to update
// visible rows in place. Untested by convention, same as
// FolderNavigationController/DownloadController: src/qml is GUI glue, and
// MegaExplorerTests only links MegaExplorerCore.
//
// enable_shared_from_this for the same reason as
// FolderNavigationController: since a tab (and this controller with it) can
// now be closed while a requestThumbnail() fetch is still in flight, the
// async callback below captures a shared_from_this() copy to stay alive
// until it runs; the queued invoke that follows is what covers the rest of
// the window (see GuiThread.h).
class ThumbnailController : public QObject, public std::enable_shared_from_this<ThumbnailController>
{
    Q_OBJECT

public:
    explicit ThumbnailController(std::shared_ptr<ThumbnailService> service,
                                 FileListModel* model,
                                 NotificationController* notifications,
                                 QObject* parent = nullptr);

    // Resolves a per-handle destination path under a session-cache temp
    // subfolder and asks ThumbnailService to fetch handle's thumbnail there,
    // then writes the result back into the row via
    // FileListModel::setThumbnailPath. Caller (QML) is expected to only
    // invoke this for rows with hasThumbnail == true && isFolder == false;
    // this method doesn't re-check those flags itself -- see .cpp for why.
    // Failures are logged (lcThumbnail) and relayed through
    // NotificationController's generic error toast, same as
    // FolderNavigationController's navigation/search failures.
    Q_INVOKABLE void requestThumbnail(quint64 handle);

private:
    QString computeDestinationPath(quint64 handle) const;

    std::shared_ptr<ThumbnailService> mService;
    FileListModel* mModel;
    NotificationController* mNotifications;
};
