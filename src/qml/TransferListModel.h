#pragma once
#include "core/DownloadService.h"
#include "core/UploadService.h"
#include "TransferEnums.h"

#include <QAbstractListModel>
#include <QString>

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

signals:
    void countChanged();

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

    std::vector<Row> mRows;
};
