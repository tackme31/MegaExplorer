#include "FileListModel.h"

FileListModel::FileListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int FileListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(mEntries.size());
}

QVariant FileListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(mEntries.size()))
        return {};

    const FileEntry& entry = mEntries[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case NameRole:
        return QString::fromStdString(entry.name);
    case SizeRole:
        return static_cast<qulonglong>(entry.sizeBytes);
    case IsFolderRole:
        return entry.isFolder;
    case HandleRole:
        // qulonglong round-trips through QML as a JS double (exact only up to
        // 2^53), same as SizeRole above; MEGA handles stay well under that.
        return static_cast<qulonglong>(entry.handle);
    default:
        return {};
    }
}

QHash<int, QByteArray> FileListModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {SizeRole, "sizeBytes"},
        {IsFolderRole, "isFolder"},
        {HandleRole, "handle"},
    };
}

void FileListModel::setEntries(std::vector<FileEntry> entries)
{
    beginResetModel();
    mEntries = std::move(entries);
    endResetModel();
}
