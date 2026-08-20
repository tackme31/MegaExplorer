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

    // Stops the one queued or active download named by a TransferListModel row's
    // jobId. No toast, unlike cancelDownloads(): the row the click landed on turns
    // to "Cancelled" in place, and one toast per clicked row would stack.
    Q_INVOKABLE void cancelJob(quint64 jobId);

    // Opens the file with the OS default application. Only ever from the snackbar's
    // "Open" button -- never auto-invoked.
    Q_INVOKABLE void openFile(QString localPath);

signals:
    void downloadActiveChanged();

    // One snapshot per job whenever that job's state or progress moves, queued jobs
    // included. TransferListModel is the consumer; nothing else may assume a job is
    // reported only once, and a cancelled or failed job is reported here as well as
    // through downloadFinished.
    void jobChanged(const DownloadJob& job);

    // Once per finished job, success or failure. localPath is meaningful only on
    // success, where it is the path actually written -- a name already taken at the
    // destination gets a "(1)" suffix rather than blocking the download.
    void downloadFinished(bool success, QString fileName, QString localPath);

private:
    void refreshActiveJob();

    // Emits jobChanged for every job still in the queue. Called where the queue
    // itself moves (a new enqueue, a job finishing) rather than on every progress
    // tick, which already carries the one job it is about.
    void publishQueue();
    QString computeDestinationPath(const QString& fileName) const;

    std::shared_ptr<DownloadService> mService;
    NotificationController* mNotifications;
    std::optional<DownloadJob> mActiveJob;
};
