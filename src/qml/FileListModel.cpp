#include "FileListModel.h"

#include "core/FileKind.h"

#include <QLocale>

FileListModel::FileListModel(QObject* parent) : QAbstractTableModel(parent) {}

int FileListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return static_cast<int>(mEntries.size());
}

int FileListModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return 4; // Name, Modified, Kind, Size -- see FileTableView.qml's header labels
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
        case HasThumbnailRole:
            return entry.hasThumbnail;
        case ThumbnailPathRole:
            return mThumbnailPaths[static_cast<std::size_t>(index.row())];
        case ModificationTimeRole:
            return static_cast<qlonglong>(entry.modificationTime);
        case FormattedSizeRole:
            // Folders show a blank size, matching Explorer's convention;
            // SizeRole above stays the raw byte count regardless (the
            // download flow depends on the actual numeric value).
            return entry.isFolder
                       ? QString()
                       : QLocale::system().formattedDataSize(static_cast<qint64>(entry.sizeBytes),
                                                             1,
                                                             QLocale::DataSizeTraditionalFormat);
        case ExtensionRole:
            return QString::fromStdString(fileExtensionUppercased(entry.name));
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
        {HasThumbnailRole, "hasThumbnail"},
        {ThumbnailPathRole, "thumbnailPath"},
        {ModificationTimeRole, "modificationTime"},
        {FormattedSizeRole, "formattedSize"},
        {ExtensionRole, "extension"},
    };
}

void FileListModel::setEntries(std::vector<FileEntry> entries)
{
    beginResetModel();
    mEntries = std::move(entries);
    mThumbnailPaths.assign(mEntries.size(), QString());
    endResetModel();
}

void FileListModel::setThumbnailPath(quint64 handle, QString path)
{
    for (std::size_t i = 0; i < mEntries.size(); ++i)
    {
        if (mEntries[i].handle == static_cast<std::uint64_t>(handle))
        {
            mThumbnailPaths[i] = std::move(path);
            const int row = static_cast<int>(i);
            emit dataChanged(index(row, 0), index(row, columnCount() - 1), {ThumbnailPathRole});
            return;
        }
    }
    // Row no longer present -- stale async result, same "ignore it" handling
    // as FolderNavigationController::applyResult for other late callbacks.
}
