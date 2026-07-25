#pragma once
#include "core/DownloadService.h"

#include <QObject>
#include <QString>

#include <memory>

// QML-facing GUI glue wrapping DownloadService, registered as its own
// "downloadController" context property (main.cpp) -- deliberately separate
// from FolderNavigationController since it's an independent concern
// (download queue vs. folder browsing). Both double-click-on-a-file and the
// context menu's "Download" item call the same downloadFile() entry point.
// Untested by convention, same as FolderNavigationController: src/qml is
// GUI glue, and MegaExplorerTests only links MegaExplorerCore.
class DownloadController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool downloadActive READ downloadActive NOTIFY downloadActiveChanged)
    Q_PROPERTY(QString activeFileName READ activeFileName NOTIFY downloadActiveChanged)
    Q_PROPERTY(qreal activeProgress READ activeProgress NOTIFY downloadActiveChanged)

public:
    explicit DownloadController(std::shared_ptr<DownloadService> service,
                                QObject* parent = nullptr);

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
    // snackbar. localPath is only meaningful when success is true;
    // errorMessage is only meaningful when success is false.
    void downloadFinished(bool success, QString fileName, QString localPath, QString errorMessage);

private:
    void refreshActiveJob();
    QString computeDestinationPath(const QString& fileName) const;

    std::shared_ptr<DownloadService> mService;
    DownloadJob mActiveJob;
    bool mHasActiveJob = false;
};
