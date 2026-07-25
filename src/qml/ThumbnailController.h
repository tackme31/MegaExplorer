#pragma once
#include "core/ThumbnailService.h"
#include "FileListModel.h"

#include <QObject>
#include <QString>

#include <memory>

// QML-facing GUI glue wrapping ThumbnailService, registered as its own
// "thumbnailController" context property (main.cpp), separate from
// FolderNavigationController/DownloadController -- see
// FolderNavigationController::fileListModelForThumbnails() for why this
// class (unlike DownloadController) is allowed to write directly into the
// FileListModel FolderNavigationController owns: thumbnails have to update
// visible rows in place. Untested by convention, same as
// FolderNavigationController/DownloadController: src/qml is GUI glue, and
// MegaExplorerTests only links MegaExplorerCore.
class ThumbnailController : public QObject
{
    Q_OBJECT

public:
    explicit ThumbnailController(std::shared_ptr<ThumbnailService> service,
                                 FileListModel* model,
                                 QObject* parent = nullptr);

    // Resolves a per-handle destination path under a session-cache temp
    // subfolder and asks ThumbnailService to fetch handle's thumbnail there,
    // then writes the result back into the row via
    // FileListModel::setThumbnailPath. Caller (QML) is expected to only
    // invoke this for rows with hasThumbnail == true && isFolder == false;
    // this method doesn't re-check those flags itself -- see .cpp for why.
    // Failures are qWarning()-only, no UI feedback (same as Phase 2/3
    // navigation/search failures), matching TASKS.md's Phase 5 scope.
    Q_INVOKABLE void requestThumbnail(quint64 handle);

private:
    QString computeDestinationPath(quint64 handle) const;

    std::shared_ptr<ThumbnailService> mService;
    FileListModel* mModel;
};
