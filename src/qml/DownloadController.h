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

    // Opens the file with the OS default application. Only ever from the snackbar's
    // "Open" button -- never auto-invoked.
    Q_INVOKABLE void openFile(QString localPath);

signals:
    void downloadActiveChanged();

    // Once per finished job, success or failure. localPath and alreadyPresent are
    // only meaningful on success. No reason field: a failure says only "couldn't
    // download <name>", so the SDK's English text stops at the qCWarning.
    //
    // alreadyPresent means the SDK found a fingerprint-identical file already there
    // and skipped the transfer, letting the snackbar say "already downloaded" rather
    // than implying a fresh file. On success fileName is the *actual* saved leaf
    // name, which a collision can make differ from the requested one -- echoing the
    // requested name would read as if the existing file got overwritten.
    void downloadFinished(bool success, QString fileName, QString localPath, bool alreadyPresent);

private:
    void refreshActiveJob();
    QString computeDestinationPath(const QString& fileName) const;

    std::shared_ptr<DownloadService> mService;
    NotificationController* mNotifications;
    std::optional<DownloadJob> mActiveJob;
};
