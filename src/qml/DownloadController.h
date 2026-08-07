#pragma once
#include "core/DownloadService.h"

#include <QObject>
#include <QString>

#include <memory>
#include <optional>

class NotificationController;

// QML-facing GUI glue wrapping DownloadService, registered as its own
// "downloadController" context property (main.cpp) -- deliberately separate
// from FolderNavigationController since it's an independent concern
// (download queue vs. folder browsing). Both double-click-on-a-file and the
// context menu's "Download" item call the same downloadFile() entry point.
class DownloadController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool downloadActive READ downloadActive NOTIFY downloadActiveChanged)
    Q_PROPERTY(QString activeFileName READ activeFileName NOTIFY downloadActiveChanged)
    Q_PROPERTY(qreal activeProgress READ activeProgress NOTIFY downloadActiveChanged)

public:
    explicit DownloadController(std::shared_ptr<DownloadService> service,
                                NotificationController* notifications,
                                QObject* parent = nullptr);
    ~DownloadController() override;

    bool downloadActive() const;
    QString activeFileName() const;
    qreal activeProgress() const; // 0.0-1.0, 0.0 if totalBytes is still unknown

    // Single entry point for both double-click-on-a-file and the context
    // menu's "Download" item. Computes a destination path in the platform's
    // Downloads folder (a Qt concern, kept out of src/core) and enqueues the
    // download via
    // DownloadService. sizeBytes (already known from the file list) seeds
    // the job's totalBytes so the progress UI has a real denominator
    // immediately, before MegaApi's first progress update arrives. No-ops
    // if handle is already queued or active, so repeat double-clicks/menu
    // selections don't queue duplicate re-downloads of the same file.
    Q_INVOKABLE void downloadFile(quint64 handle, QString name, quint64 sizeBytes);

    // Opens localPath (as reported by downloadFinished's localPath
    // argument) with the OS default application. Only ever called from the
    // snackbar's "Open" button -- never auto-invoked.
    Q_INVOKABLE void openFile(QString localPath);

signals:
    void downloadActiveChanged();

    // Fired once per finished job, success or failure -- drives the
    // snackbar. localPath and alreadyPresent are only meaningful when success
    // is true. No reason field: a failed download says only "couldn't download
    // <name>", so the SDK's English message and its errorCode stop at the
    // qCWarning in the handler (R5-10; same call as R3-4 made for openFile).
    // alreadyPresent is true when an identical (fingerprint-matching) file was
    // already at the destination and the SDK skipped the transfer instead of
    // downloading/renaming/overwriting -- lets the snackbar say "already
    // downloaded" rather than implying a fresh (or overwritten) file. On
    // success, fileName is the *actual* saved leaf name (localPath's basename),
    // not necessarily what downloadFile() was originally called with -- a name
    // collision with different-content local file can make the SDK rename the
    // saved file (e.g. "photo (1).jpg"), and echoing the pre-rename name here
    // would read as if the original file got overwritten. On failure, no file
    // was saved, so fileName is simply what was requested.
    void downloadFinished(bool success, QString fileName, QString localPath, bool alreadyPresent);

private:
    void refreshActiveJob();
    QString computeDestinationPath(const QString& fileName) const;

    std::shared_ptr<DownloadService> mService;
    NotificationController* mNotifications;
    std::optional<DownloadJob> mActiveJob;
};
