#pragma once
#include "core/DownloadService.h"
#include "core/UploadService.h"
#include "TransferEnums.h"

#include <QAbstractListModel>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <vector>

class DownloadController;
class UploadController;

// The whole transfer queue as one list: downloads and uploads interleaved in the
// order they were enqueued, one row each.
//
// It keeps finished rows for the rest of the session. DownloadService and
// UploadService are live queues -- a job is dropped the moment its onJobFinished
// fires -- so retaining history is this model's job, not theirs.
//
// Fed from DownloadController/UploadController rather than from the two services
// directly: their observer slots are single-subscriber and those controllers already
// hold them, and going through the controllers also puts every update on the GUI
// thread (the services call back from an SDK-internal thread).
class TransferListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

    // Summary of the current *run* -- the group of transfers starting with the
    // first job enqueued while nothing was in flight and ending once every row in
    // it has settled. The whole-model tallies would be the wrong thing to show:
    // finished rows stay for the session, so the denominator would keep climbing
    // across unrelated bursts.
    Q_PROPERTY(bool runActive READ runActive NOTIFY runChanged)
    Q_PROPERTY(int runTotal READ runTotal NOTIFY runChanged)
    Q_PROPERTY(int runFinished READ runFinished NOTIFY runChanged)
    Q_PROPERTY(qreal runProgress READ runProgress NOTIFY runChanged)

public:
    enum Roles
    {
        DirectionRole = Qt::UserRole + 1, // TransferDirection.Download / .Upload
        NameRole,
        StateRole, // TransferState.*
        ProgressRole,
        TransferredBytesRole,
        TotalBytesRole,
    };

    explicit TransferListModel(DownloadController* downloads,
                               UploadController* uploads,
                               QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;

    bool runActive() const;
    int runTotal() const;
    int runFinished() const;
    qreal runProgress() const; // 0.0-1.0, the run's rows averaged

signals:
    void countChanged();
    void runChanged();

private:
    struct Row
    {
        TransferDirectionEnum::Kind direction = TransferDirectionEnum::Download;
        std::uint64_t jobId = 0;
        QString name;
        TransferStateEnum::Kind state = TransferStateEnum::Queued;
        std::uint64_t transferredBytes = 0;
        std::uint64_t totalBytes = 0;
    };

    void recordDownload(const DownloadJob& job);
    void recordUpload(const UploadJob& job);

    // Appends on first sight of a job, updates in place afterwards. Job ids restart
    // at 1 per service, so the direction is part of the key.
    void upsert(const Row& row);

    static bool isSettled(const Row& row);
    static qreal rowProgress(const Row& row);

    // Recomputes the four run properties and notifies. Called from upsert only
    // where something actually changed, so it may notify unconditionally.
    void refreshRun();

    std::vector<Row> mRows;

    std::size_t mRunStart = 0;
    bool mRunActive = false;
    int mRunTotal = 0;
    int mRunFinished = 0;
    qreal mRunProgress = 0.0;
};
