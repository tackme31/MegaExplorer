#pragma once
#include "core/DownloadService.h"

#include <QObject>
#include <QString>

#include <memory>
#include <optional>

class NotificationController;

// QML-facing GUI glue wrapping DownloadService. App-global and deliberately
// separate from folder browsing.
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

    // Single entry point for double-click and the menu's "Download". Computes the
    // destination in the platform's Downloads folder -- a Qt concern kept out of
    // src/core. sizeBytes seeds totalBytes so the progress UI has a denominator
    // before the first SDK update. No-ops if the handle is already queued or active.
    Q_INVOKABLE void downloadFile(quint64 handle, QString name, quint64 sizeBytes);

    // Stops the whole download queue, active transfer included. The toast is raised
    // from here, once, with the queue length as it stands now -- the alternative
    // (tallying the Cancelled jobs as they land) would have to wait for the SDK to
    // acknowledge the abort, leaving the button looking dead in the meantime.
    Q_INVOKABLE void cancelDownloads();

    // Opens the file with the OS default application. Only ever from the snackbar's
    // "Open" button -- never auto-invoked.
    Q_INVOKABLE void openFile(QString localPath);

signals:
    void downloadActiveChanged();

    // Once per finished job, success or failure. localPath is meaningful only on
    // success, where it is the path actually written -- a name already taken at the
    // destination gets a "(1)" suffix rather than blocking the download.
    void downloadFinished(bool success, QString fileName, QString localPath);

private:
    void refreshActiveJob();
    QString computeDestinationPath(const QString& fileName) const;

    std::shared_ptr<DownloadService> mService;
    NotificationController* mNotifications;
    std::optional<DownloadJob> mActiveJob;
};
