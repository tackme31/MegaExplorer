#pragma once
#include "core/UploadScanService.h"
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

class NotificationController;

// QML-facing GUI glue wrapping UploadService. App-global rather than per-tab
// because three of the five drop targets (folder tree, quick-access pins,
// breadcrumb) belong to chrome shared by every tab, so they have no owning tab.
//
// Also owns the decisions a drop can require before anything is enqueued, in this
// order: the file cap (an outright refusal, not a question), then "upload N
// files?", then replace/skip same-named files. Each question is asked through a
// signal so QML can raise a dialog and answer by calling the matching
// Q_INVOKABLE.
class UploadController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool uploadActive READ uploadActive NOTIFY uploadActiveChanged)
    Q_PROPERTY(QString activeFileName READ activeFileName NOTIFY uploadActiveChanged)
    Q_PROPERTY(qreal activeProgress READ activeProgress NOTIFY uploadActiveChanged)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY uploadActiveChanged)
    Q_PROPERTY(int maxFilesPerUpload READ maxFilesPerUpload CONSTANT)

public:
    // One drop can name far more files than a person means to send -- a dropped
    // folder especially, since it is counted by what is inside it. The cap is on the
    // whole operation rather than per folder, so plain multi-select is bounded too.
    static constexpr int kMaxFilesPerUpload = 100;

    explicit UploadController(std::shared_ptr<UploadService> service,
                              std::shared_ptr<UploadScanService> scan,
                              NotificationController* notifications,
                              QObject* parent = nullptr);
    ~UploadController() override;

    bool uploadActive() const;
    QString activeFileName() const;
    qreal activeProgress() const; // 0.0-1.0, 0.0 if totalBytes is still unknown

    // Whole queue length, active job included.
    int pendingCount() const;

    // Read by ToastStack so the rejection message names the same number this
    // enforces.
    int maxFilesPerUpload() const;

    // What a hovered drop target paints its feedback from, so it is synchronous all
    // the way down to IMegaClient::checkUpload.
    Q_INVOKABLE bool canUploadTo(quint64 target, bool targetIsRoot) const;

    // Every DropArea's entry point for an external (OS) drop. Separates local files
    // and folders from items that are neither -- which QML can't do -- then hands
    // the rest on, or reports that nothing was uploadable.
    Q_INVOKABLE void dropUrls(const QList<QUrl>& urls, quint64 target, bool targetIsRoot);

    // The single enqueue path: applies the file cap, then asks to confirm a
    // multi-file upload, then the same-name check. localPaths may name folders,
    // which go to the SDK whole and upload recursively.
    Q_INVOKABLE void uploadFiles(const QStringList& localPaths, quint64 target, bool targetIsRoot);

    // "Yes" to uploadRequiresConfirmation. Re-applies the cap rather than trusting
    // the count that opened the dialog, for the same reason the two below re-derive
    // their collision set.
    Q_INVOKABLE void
    uploadConfirmed(const QStringList& localPaths, quint64 target, bool targetIsRoot);

    // Replacing is a plain upload: MEGA stacks a same-named file as a new version of
    // the existing node, so there is nothing for the app to delete afterwards.
    Q_INVOKABLE void
    uploadReplacingExisting(const QStringList& localPaths, quint64 target, bool targetIsRoot);

    // Re-derives the collision set rather than trust names round-tripped through QML,
    // which also picks up destination changes made while the dialog was open. Cheap --
    // it is an in-memory lookup. A collision nested inside a dropped folder makes this
    // walk that branch itself, since the SDK's recursive upload cannot leave one file
    // out (SPEC_NAME_CONFLICT_UPLOAD.md 5-2).
    Q_INVOKABLE void
    uploadSkippingExisting(const QStringList& localPaths, quint64 target, bool targetIsRoot);

    // Stops the whole upload queue, active transfer included. Reported once from
    // here for the same reason as DownloadController::cancelDownloads(). Files
    // already uploaded stay up: MEGA has no transactional batch, and a cancel is a
    // request to stop, not to undo.
    Q_INVOKABLE void cancelUploads();

    // Not QML-facing: TabsController folds this into a tab's busy role, since an
    // upload has no owning tab to drive FolderNavigationController::busy.
    bool isUploadingTo(quint64 handle, bool isRoot) const;

signals:
    void uploadActiveChanged();

    // One snapshot per job whenever that job's state or progress moves, queued jobs
    // included. TransferListModel is the consumer; nothing else may assume a job is
    // reported only once, and a cancelled or failed job is reported here as well as
    // through the batch notification.
    void jobChanged(const UploadJob& job);

    // Membership only -- not emitted per file, since nothing binds to the count.
    void activeDestinationsChanged();

    // More than one file is about to go up. fileCount is the recursive count -- a
    // dropped folder is worth what is inside it -- so it can exceed filePaths'
    // length. Never emitted once the cap has already rejected the drop: that is a
    // refusal, and stacking a question on top of it would ask about an upload that
    // is not going to happen either way.
    void uploadRequiresConfirmation(QStringList filePaths,
                                    int fileCount,
                                    quint64 destinationHandle,
                                    bool destinationIsRoot);

    // Some files in this upload already exist in the destination, whether they were
    // dropped directly or sit inside a dropped folder. filePaths is the *whole* set
    // of dropped paths -- the answer handlers re-derive which is which.
    // conflictNames and unaffectedCount are for the dialog's wording only:
    // conflictNames spells a nested hit out as "folder/sub/name", and
    // unaffectedCount is how many files Skip would still upload, without which
    // Skip and Cancel read the same (spec 3-0).
    //
    // The two sizes go with those two counts and are already formatted, because QML
    // has no formattedDataSize equivalent; either is empty when it is zero, and the
    // dialog then leaves that parenthetical out (spec 1-6, 3-3).
    void nameConflictRequiresConfirmation(QStringList filePaths,
                                          QStringList conflictNames,
                                          int unaffectedCount,
                                          QString conflictingSize,
                                          QString unaffectedSize,
                                          quint64 destinationHandle,
                                          bool destinationIsRoot);

    // A batch finished entirely -- queue drained -- emitted once per destination it
    // touched.
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
        // Counted here rather than read off UploadService's queue length: the
        // finished notifications arrive through a queued invoke, so the queue can
        // already be empty when the first is handled -- flushing once per job.
        int pendingJobs = 0;
        std::set<Destination> destinations;
        // Unfinished work per destination, for isUploadingTo(). Separate from
        // destinations above, which is success-only and fills in as jobs land -- a
        // spinner has to go up when the work is *enqueued*.
        std::map<Destination, int> pendingByDestination;
    };

    struct UploadVolume
    {
        int files = 0;
        qint64 bytes = 0;
    };

    // What the cap is measured against: dropped files count as one each, a dropped
    // folder as everything inside it. Stops counting once past the cap, so a
    // 200k-file tree costs the same as a 101-file one -- which truncates bytes too,
    // so only read those once files is known to be within the cap.
    UploadVolume expandedVolume(const QStringList& localPaths) const;

    // The half of uploadFiles() after the two gates: enqueue, or ask about
    // same-named files first. Shared with uploadConfirmed().
    void askAboutConflicts(const QStringList& localPaths, quint64 target, bool targetIsRoot);

    void enqueueAll(const QStringList& localPaths, quint64 target, bool targetIsRoot);

    // The one place jobs reach UploadService. Each item carries its own destination,
    // because a skip that walked into a dropped folder places files in the MEGA
    // folders they merged with, not in the drop target.
    void enqueuePlan(const std::vector<UploadPlanItem>& plan);

    // Empty if the lookup itself failed: that must not block the upload, it only
    // means no replace/skip question gets asked. Only files ever appear -- a folder
    // whose name is taken merges into the existing one instead of versioning it, so
    // the walk descends through it rather than asking about it.
    std::vector<UploadCollision>
    collisionsFor(const QStringList& localPaths, quint64 target, bool targetIsRoot) const;

    void refreshActiveJob();

    // Emits jobChanged for every job still in the queue. Called where the queue
    // itself moves (a new batch enqueued, a job finishing) rather than on every
    // progress tick, which already carries the one job it is about.
    void publishQueue();

    void flushBatchIfDone();

    // The only two places pendingByDestination changes: each job is retained once and
    // released once.
    void retainDestination(const Destination& destination, int count);
    void releaseDestination(const Destination& destination);

    std::shared_ptr<UploadService> mService;
    std::shared_ptr<UploadScanService> mScan;
    NotificationController* mNotifications;
    std::optional<UploadJob> mActiveJob;
    Batch mBatch;
};
