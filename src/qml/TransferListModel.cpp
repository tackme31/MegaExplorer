#include "TransferListModel.h"

#include "DownloadController.h"
#include "UploadController.h"

TransferListModel::TransferListModel(DownloadController* downloads,
                                     UploadController* uploads,
                                     QObject* parent)
    : QAbstractListModel(parent)
{
    if (downloads)
        connect(
            downloads, &DownloadController::jobChanged, this, &TransferListModel::recordDownload);
    if (uploads)
        connect(uploads, &UploadController::jobChanged, this, &TransferListModel::recordUpload);
}

int TransferListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(mRows.size());
}

QVariant TransferListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(mRows.size()))
        return {};
    const Row& row = mRows[static_cast<std::size_t>(index.row())];
    switch (role)
    {
        case DirectionRole:
            return static_cast<int>(row.direction);
        case NameRole:
            return row.name;
        case StateRole:
            return static_cast<int>(row.state);
        case ProgressRole:
            return rowProgress(row);
        case TransferredBytesRole:
            return static_cast<qulonglong>(row.transferredBytes);
        case TotalBytesRole:
            return static_cast<qulonglong>(row.totalBytes);
        default:
            return {};
    }
}

QHash<int, QByteArray> TransferListModel::roleNames() const
{
    return {
        {DirectionRole, "direction"},
        {NameRole, "name"},
        {StateRole, "state"},
        {ProgressRole, "progress"},
        {TransferredBytesRole, "transferredBytes"},
        {TotalBytesRole, "totalBytes"},
    };
}

int TransferListModel::count() const
{
    return static_cast<int>(mRows.size());
}

bool TransferListModel::runActive() const
{
    return mRunActive;
}

int TransferListModel::runTotal() const
{
    return mRunTotal;
}

int TransferListModel::runFinished() const
{
    return mRunFinished;
}

qreal TransferListModel::runProgress() const
{
    return mRunProgress;
}

void TransferListModel::recordDownload(const DownloadJob& job)
{
    // job.name throughout, including on success, even though the file may have been
    // saved under a "(1)" suffix: a row that renames itself as it completes is worse
    // to read than one that keeps naming the node that was asked for.
    upsert(Row{TransferDirectionEnum::Download,
               job.id,
               QString::fromStdString(job.name),
               static_cast<TransferStateEnum::Kind>(job.state),
               job.transferredBytes,
               job.totalBytes});
}

void TransferListModel::recordUpload(const UploadJob& job)
{
    upsert(Row{TransferDirectionEnum::Upload,
               job.id,
               QString::fromStdString(job.name),
               static_cast<TransferStateEnum::Kind>(job.state),
               job.transferredBytes,
               job.totalBytes});
}

void TransferListModel::upsert(const Row& row)
{
    for (std::size_t i = 0; i < mRows.size(); ++i)
    {
        Row& existing = mRows[i];
        if (existing.direction != row.direction || existing.jobId != row.jobId)
            continue;
        if (existing.name == row.name && existing.state == row.state &&
            existing.transferredBytes == row.transferredBytes &&
            existing.totalBytes == row.totalBytes)
            return;
        existing = row;
        const QModelIndex changed = index(static_cast<int>(i));
        emit dataChanged(changed, changed);
        refreshRun();
        return;
    }

    // A job arriving while the previous run has already settled opens a new run
    // rather than joining it, so the flyout's denominator is what is in flight now
    // and not everything this session has ever transferred.
    if (!mRunActive)
        mRunStart = mRows.size();

    const int at = static_cast<int>(mRows.size());
    beginInsertRows(QModelIndex(), at, at);
    mRows.push_back(row);
    endInsertRows();
    emit countChanged();
    refreshRun();
}

bool TransferListModel::isSettled(const Row& row)
{
    return row.state == TransferStateEnum::Completed || row.state == TransferStateEnum::Failed ||
           row.state == TransferStateEnum::Cancelled;
}

qreal TransferListModel::rowProgress(const Row& row)
{
    // A finished row reads 1.0 even when totalBytes stayed 0: an empty file never
    // reports a byte, and a progress bar left at 0 on a row labelled Completed
    // reads as a stalled transfer.
    if (row.state == TransferStateEnum::Completed)
        return 1.0;
    if (row.totalBytes == 0)
        return 0.0;
    return static_cast<qreal>(row.transferredBytes) / static_cast<qreal>(row.totalBytes);
}

void TransferListModel::refreshRun()
{
    int total = 0;
    int finished = 0;
    qreal sum = 0.0;
    for (std::size_t i = mRunStart; i < mRows.size(); ++i)
    {
        ++total;
        if (isSettled(mRows[i]))
            ++finished;
        sum += rowProgress(mRows[i]);
    }

    mRunTotal = total;
    mRunFinished = finished;
    mRunActive = finished < total;
    // Rows averaged rather than bytes summed: a queued row's size is a guess until
    // the SDK confirms it, and a byte denominator that moves under the bar makes it
    // walk backwards. This way the bar and the "9 / 41" beside it agree.
    mRunProgress = total == 0 ? 0.0 : sum / total;
    emit runChanged();
}
