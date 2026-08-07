#pragma once
#include "core/UploadService.h"

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <map>
#include <memory>
#include <optional>
#include <set>

class FileOperationService;
class NotificationController;

// QML-facing GUI glue wrapping UploadService. App-global rather than per-tab
// because three of the five drop targets (folder tree, quick-access pins,
// breadcrumb) belong to chrome shared by every tab, so they have no owning tab.
//
// Also owns the two decisions a drop can require before anything is enqueued, in
// this order: skip-the-folders (dropUrls), then replace/skip same-named files
// (uploadFiles). Both are asked through signals so QML can raise a dialog and
// answer by calling the matching Q_INVOKABLE.
class UploadController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool uploadActive READ uploadActive NOTIFY uploadActiveChanged)
    Q_PROPERTY(QString activeFileName READ activeFileName NOTIFY uploadActiveChanged)
    Q_PROPERTY(qreal activeProgress READ activeProgress NOTIFY uploadActiveChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY uploadActiveChanged)

public:
    explicit UploadController(std::shared_ptr<UploadService> service,
                              std::shared_ptr<FileOperationService> fileOperations,
                              NotificationController* notifications,
                              QObject* parent = nullptr);
    ~UploadController() override;

    bool uploadActive() const;
    QString activeFileName() const;
    qreal activeProgress() const; // 0.0-1.0, 0.0 if totalBytes is still unknown

    // Whole queue length, active job included.
    int pendingCount() const;

    // What a hovered drop target paints its feedback from, so it is synchronous all
    // the way down to IMegaClient::checkUpload.
    Q_INVOKABLE bool canUploadTo(quint64 target, bool targetIsRoot) const;

    // Every DropArea's entry point for an external (OS) drop. Classifies urls into
    // uploadable files, folders and non-local items -- which QML can't do -- then
    // enqueues, asks for the folder-skip confirmation, or reports nothing uploadable.
    Q_INVOKABLE void dropUrls(const QList<QUrl>& urls, quint64 target, bool targetIsRoot);

    // Entry point past the folder decision -- from dropUrls when the drop held no
    // folders, or from the folder-skip dialog. Runs the same-name check next.
    Q_INVOKABLE void uploadFiles(const QStringList& localPaths, quint64 target, bool targetIsRoot);

    // Both re-derive the collision set rather than trust handles round-tripped
    // through QML, which also picks up destination changes made while the dialog was
    // open. Cheap -- it is an in-memory lookup.
    Q_INVOKABLE void
    uploadReplacingExisting(const QStringList& localPaths, quint64 target, bool targetIsRoot);
    Q_INVOKABLE void
    uploadSkippingExisting(const QStringList& localPaths, quint64 target, bool targetIsRoot);

    // Not QML-facing: TabsController folds this into a tab's busy role, since an
    // upload has no owning tab to drive FolderNavigationController::busy.
    bool isUploadingTo(quint64 handle, bool isRoot) const;

signals:
    void uploadActiveChanged();

    // Membership only -- not emitted per file, since nothing binds to the count.
    void activeDestinationsChanged();

    // filePaths is the uploadable remainder and the destination travels with it, so
    // no state is kept here while the dialog is open.
    void folderDropRequiresConfirmation(QStringList filePaths,
                                        int folderCount,
                                        quint64 destinationHandle,
                                        bool destinationIsRoot);

    // Some of filePaths name files that already exist in the destination.
    // filePaths is the *whole* set, conflicting or not -- the answer handlers
    // re-derive which is which. conflictNames is for the dialog's wording only.
    void nameConflictRequiresConfirmation(QStringList filePaths,
                                          QStringList conflictNames,
                                          quint64 destinationHandle,
                                          bool destinationIsRoot);

    // A batch finished entirely -- queue drained, every replaced node dealt with --
    // emitted once per destination it touched.
    void destinationChanged(quint64 handle, bool isRoot);

private:
    // Destination as a map key; isRoot keeps root and "the node whose handle is 0"
    // from colliding.
    using Destination = std::pair<quint64, bool>;

    // Running tally since the queue last drained. A second drop arriving mid-flight
    // joins the same batch on purpose: one snackbar beats two competing ones.
    struct Batch
    {
        int succeeded = 0;
        int failed = 0;
        int destinationGone = 0; // failures whose code was kENoEnt
        int replaceFailed = 0;
        // Counted here rather than read off UploadService's queue length: the
        // finished notifications arrive through a queued invoke, so the queue can
        // already be empty when the first is handled -- flushing once per job.
        int pendingJobs = 0;
        int pendingReplaces = 0; // Rubbish-bin moves still in flight
        std::set<Destination> destinations;
        // Unfinished work per destination, for isUploadingTo(). Separate from
        // destinations above, which is success-only and fills in as jobs land -- a
        // spinner has to go up when the work is *enqueued*.
        std::map<Destination, int> pendingByDestination;
    };

    void enqueueAll(const QStringList& localPaths,
                    const std::map<QString, quint64>& replaceHandleByName,
                    quint64 target,
                    bool targetIsRoot);

    // Empty if the lookup itself failed: that must not block the upload, it only
    // means no replace/skip question gets asked.
    std::map<QString, quint64>
    collisionsFor(const QStringList& localPaths, quint64 target, bool targetIsRoot) const;

    void refreshActiveJob();
    void flushBatchIfDone();

    // The only two places pendingByDestination changes: each job is retained once and
    // released once. A replace hands its release to the Rubbish-bin move that
    // follows, so the destination keeps spinning until the old node is gone.
    void retainDestination(const Destination& destination, int count);
    void releaseDestination(const Destination& destination);

    std::shared_ptr<UploadService> mService;
    std::shared_ptr<FileOperationService> mFileOps;
    NotificationController* mNotifications;
    std::optional<UploadJob> mActiveJob;
    Batch mBatch;
};
