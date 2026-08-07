#pragma once
#include "core/ThumbnailService.h"
#include "FileListModel.h"

#include <QObject>
#include <QString>

#include <memory>

class NotificationController;

// QML-facing GUI glue wrapping ThumbnailService, one per tab. Unlike
// DownloadController it writes directly into the tab's FileListModel, because
// thumbnails have to update visible rows in place. That model is *shared* rather
// than borrowed: enable_shared_from_this keeps this object alive, not the one whose
// interior a raw pointer would name.
//
// A tab can close while a fetch is in flight, so the callback captures
// shared_from_this(); since that copy is destroyed on the SDK thread, instances are
// created through GuiThread.h's makeGuiOwned.
class ThumbnailController : public QObject, public std::enable_shared_from_this<ThumbnailController>
{
    Q_OBJECT

public:
    explicit ThumbnailController(std::shared_ptr<ThumbnailService> service,
                                 std::shared_ptr<FileListModel> model,
                                 NotificationController* notifications,
                                 QObject* parent = nullptr);

    // Fetches into a per-handle path under the session cache, then writes the result
    // back into the row. Callers must only invoke this for rows with hasThumbnail &&
    // !isFolder -- this method does not re-check (see the .cpp for why).
    Q_INVOKABLE void requestThumbnail(quint64 handle);

private:
    QString computeDestinationPath(quint64 handle) const;

    std::shared_ptr<ThumbnailService> mService;
    std::shared_ptr<FileListModel> mModel;
    NotificationController* mNotifications;
};
