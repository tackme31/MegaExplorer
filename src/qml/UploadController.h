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

// QML-facing GUI glue wrapping UploadService, registered as its own
// "uploadController" context property (main.cpp) -- app-global like
// DownloadController, not per-tab, because three of the five drop targets
// (folder tree, quick-access pins, breadcrumb) belong to chrome that is
// shared by every tab and so has no owning tab at all.
//
// Also owns the two decisions a drop can require before anything is
// enqueued, in this order: skip-the-folders (dropUrls) and then
// replace/skip same-named files (uploadFiles). Both are asked through
// signals so Main.qml can raise a dialog and answer back by calling the
// matching Q_INVOKABLE.
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

    // Whole queue length, active job included -- the only progress cue a
    // serial queue can offer beyond the active file's own bar.
    int pendingCount() const;

    // Whether a drop onto (target, targetIsRoot) would be accepted -- the
    // upload counterpart of FolderNavigationController::canDropHandlesOn,
    // and what a hovered drop target paints its feedback from. Synchronous
    // all the way down to IMegaClient::checkUpload.
    Q_INVOKABLE bool canUploadTo(quint64 target, bool targetIsRoot) const;

    // The single entry point every one of the five DropAreas uses for an
    // external (OS) drop. Classifies urls into uploadable files vs. folders
    // vs. non-local items -- something QML can't do -- then either enqueues
    // straight away, asks for the folder-skip confirmation, or reports that
    // there was nothing uploadable at all.
    Q_INVOKABLE void dropUrls(const QList<QUrl>& urls, quint64 target, bool targetIsRoot);

    // Entry point past the folder decision: called by dropUrls when the drop
    // held no folders, and by Main.qml's folder-skip dialog when the user
    // chose to upload the files anyway. Performs the same-name check and
    // either enqueues or raises nameConflictRequiresConfirmation.
    Q_INVOKABLE void uploadFiles(const QStringList& localPaths, quint64 target, bool targetIsRoot);

    // The same-name dialog's two non-cancel answers. Both re-derive the
    // collision set rather than trusting handles round-tripped through QML,
    // which also picks up any change to the destination made while the
    // dialog was open -- cheap, since it's an in-memory lookup.
    Q_INVOKABLE void
    uploadReplacingExisting(const QStringList& localPaths, quint64 target, bool targetIsRoot);
    Q_INVOKABLE void
    uploadSkippingExisting(const QStringList& localPaths, quint64 target, bool targetIsRoot);

    // Whether anything is still queued or in flight for (handle, isRoot).
    // Not QML-facing: TabsController reads it to fold uploads into a tab's
    // busy role, since an upload has no owning tab of its own (see the class
    // comment) and so can't drive FolderNavigationController::busy the way
    // that tab's own mutations do.
    bool isUploadingTo(quint64 handle, bool isRoot) const;

signals:
    void uploadActiveChanged();

    // The set isUploadingTo() answers from gained or lost a destination.
    // Membership only -- not emitted per file, since nothing binds to the
    // count.
    void activeDestinationsChanged();

    // The drop contained folderCount folders, which this app can't upload.
    // filePaths carries the uploadable remainder and destinationHandle/
    // destinationIsRoot the drop target, so the answer needs no state kept
    // here while the dialog is open (same shape as Main.qml's missingPinDialog).
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

    // A batch finished entirely (queue drained and every replaced node dealt
    // with); emitted once per destination it touched. Tabs listen and refresh
    // themselves if they happen to be showing that folder.
    void destinationChanged(quint64 handle, bool isRoot);

private:
    // Destination identity as a map key: (handle, isRoot) with isRoot as the
    // usual sentinel, so root and "the node whose handle is 0" can't collide.
    using Destination = std::pair<quint64, bool>;

    // Running tally of everything dropped since the queue last drained. A
    // second drop arriving mid-flight joins the same batch on purpose: one
    // snackbar beats two competing ones.
    struct Batch
    {
        int succeeded = 0;
        int failed = 0;
        int destinationGone = 0; // failures whose code was kENoEnt
        int replaceFailed = 0;
        // Enqueued jobs whose outcome hasn't been accounted for yet. Counted
        // here rather than read off UploadService's queue length: the finished
        // notifications arrive through a queued invoke, so by the time the
        // first one is handled the queue itself can already be empty, which
        // would flush the batch once per job instead of once per batch.
        int pendingJobs = 0;
        int pendingReplaces = 0; // Rubbish-bin moves still in flight
        std::set<Destination> destinations;
        // Unfinished work per destination, for isUploadingTo(). Separate from
        // destinations above, which is deliberately success-only (it drives
        // the refresh fan-out) and populated only once a job lands -- a
        // spinner has to go up when the work is *enqueued*.
        std::map<Destination, int> pendingByDestination;
    };

    void enqueueAll(const QStringList& localPaths,
                    const std::map<QString, quint64>& replaceHandleByName,
                    quint64 target,
                    bool targetIsRoot);

    // Collisions in the destination for localPaths' leaf names, keyed by name.
    // Empty if the lookup itself failed -- a failed lookup must not block the
    // upload, it just means no replace/skip question gets asked.
    std::map<QString, quint64>
    collisionsFor(const QStringList& localPaths, quint64 target, bool targetIsRoot) const;

    void refreshActiveJob();
    void flushBatchIfDone();

    // The only two places pendingByDestination changes. Each enqueued job is
    // retained once and released once -- a replace hands its release over to
    // the Rubbish-bin move that follows it, so the destination keeps spinning
    // until the old node is actually gone.
    void retainDestination(const Destination& destination, int count);
    void releaseDestination(const Destination& destination);

    std::shared_ptr<UploadService> mService;
    std::shared_ptr<FileOperationService> mFileOps;
    NotificationController* mNotifications;
    std::optional<UploadJob> mActiveJob;
    Batch mBatch;
};
