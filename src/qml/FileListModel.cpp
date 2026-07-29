#include "FileListModel.h"

#include <QLocale>

#include <algorithm>

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
    return 3; // Name, Modified, Size -- see FileTableView.qml's header labels
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
        case SelectedRole:
            return mSelectedHandles.count(static_cast<quint64>(entry.handle)) > 0;
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
        {SelectedRole, "selected"},
    };
}

void FileListModel::setEntries(std::vector<FileEntry> entries)
{
    beginResetModel();
    mEntries = std::move(entries);
    mThumbnailPaths.assign(mEntries.size(), QString());
    endResetModel();

    pruneSelection();
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

void FileListModel::selectRow(int row, int modifiers)
{
    if (row < 0 || row >= static_cast<int>(mEntries.size()))
        return;

    const auto mods = static_cast<Qt::KeyboardModifiers>(modifiers);
    const quint64 handle = static_cast<quint64>(mEntries[static_cast<std::size_t>(row)].handle);

    if (mods.testFlag(Qt::ShiftModifier) && mAnchorHandle)
    {
        int anchorRow = rowForHandle(*mAnchorHandle);
        if (anchorRow < 0)
            anchorRow = row; // anchor's row is gone -- fall back to just this row

        const int lo = std::min(anchorRow, row);
        const int hi = std::max(anchorRow, row);
        mSelectedHandles.clear();
        for (int i = lo; i <= hi; ++i)
            mSelectedHandles.insert(
                static_cast<quint64>(mEntries[static_cast<std::size_t>(i)].handle));
    }
    else if (mods.testFlag(Qt::ControlModifier))
    {
        if (mSelectedHandles.count(handle))
            mSelectedHandles.erase(handle);
        else
            mSelectedHandles.insert(handle);
        mAnchorHandle = handle;
    }
    else
    {
        mSelectedHandles.clear();
        mSelectedHandles.insert(handle);
        mAnchorHandle = handle;
    }

    // Shift-click moves the cursor too, unlike the anchor.
    mCursorHandle = handle;

    notifySelectionChanged();
}

void FileListModel::clearSelection()
{
    if (mSelectedHandles.empty())
        return;

    mSelectedHandles.clear();
    mAnchorHandle.reset();
    mCursorHandle.reset();

    notifySelectionChanged();
}

QVariantList FileListModel::selectedHandlesVariant() const
{
    QVariantList result;
    result.reserve(static_cast<qsizetype>(mSelectedHandles.size()));
    for (quint64 handle : mSelectedHandles)
        result.append(handle);
    return result;
}

void FileListModel::pruneSelection()
{
    if (mSelectedHandles.empty())
        return;

    std::unordered_set<quint64> stillPresent;
    for (const FileEntry& entry : mEntries)
    {
        const quint64 handle = static_cast<quint64>(entry.handle);
        if (mSelectedHandles.count(handle))
            stillPresent.insert(handle);
    }

    if (stillPresent.size() != mSelectedHandles.size())
    {
        mSelectedHandles = std::move(stillPresent);
        emit selectionChanged();
    }

    if (mAnchorHandle && !mSelectedHandles.count(*mAnchorHandle))
        mAnchorHandle.reset();

    // Cursor prunes on row existence, not selection membership: a Ctrl-click
    // toggling the last selected row off leaves the cursor on that row with
    // an empty selection, and that's meant to survive.
    if (mCursorHandle && rowForHandle(*mCursorHandle) < 0)
        mCursorHandle.reset();
}


void FileListModel::selectAll()
{
    if (mEntries.empty())
        return;

    mSelectedHandles.clear();
    for (const FileEntry& entry : mEntries)
        mSelectedHandles.insert(static_cast<quint64>(entry.handle));

    // Ctrl+A doesn't move Explorer's focus rectangle -- keep the anchor/
    // cursor where they are if they still point at a live row, else seed
    // both from row 0.
    const bool anchorValid = mAnchorHandle && rowForHandle(*mAnchorHandle) >= 0;
    const bool cursorValid = mCursorHandle && rowForHandle(*mCursorHandle) >= 0;
    if (!anchorValid || !cursorValid)
    {
        const quint64 firstHandle = static_cast<quint64>(mEntries.front().handle);
        if (!anchorValid)
            mAnchorHandle = firstHandle;
        if (!cursorValid)
            mCursorHandle = firstHandle;
    }

    notifySelectionChanged();
}

void FileListModel::moveCursor(int delta, int modifiers)
{
    // Nothing selected -- arrow keys stay inert (rather than resurrecting a
    // stale cursor onto a selection nobody asked for).
    if (mSelectedHandles.empty() || !mCursorHandle)
        return;

    const int from = rowForHandle(*mCursorHandle);
    if (from < 0)
        return;

    const int lastRow = static_cast<int>(mEntries.size()) - 1;
    const int target = std::clamp(from + delta, 0, lastRow);
    const quint64 targetHandle =
        static_cast<quint64>(mEntries[static_cast<std::size_t>(target)].handle);

    const auto mods = static_cast<Qt::KeyboardModifiers>(modifiers);
    if (mods.testFlag(Qt::ShiftModifier))
    {
        // Range from the anchor (not from), so repeated Shift+arrow grows/
        // shrinks the same range instead of chasing the cursor.
        int anchorRow = mAnchorHandle ? rowForHandle(*mAnchorHandle) : -1;
        if (anchorRow < 0)
            anchorRow = from;

        const int lo = std::min(anchorRow, target);
        const int hi = std::max(anchorRow, target);
        mSelectedHandles.clear();
        for (int i = lo; i <= hi; ++i)
            mSelectedHandles.insert(
                static_cast<quint64>(mEntries[static_cast<std::size_t>(i)].handle));
    }
    else
    {
        // Ctrl is intentionally not inspected here -- Ctrl+arrow behaves
        // like a plain arrow.
        mSelectedHandles.clear();
        mSelectedHandles.insert(targetHandle);
        mAnchorHandle = targetHandle;
    }

    mCursorHandle = targetHandle;
    notifySelectionChanged();
}

int FileListModel::cursorRow() const
{
    if (!mCursorHandle)
        return -1;
    return rowForHandle(*mCursorHandle);
}

int FileListModel::rowForHandle(quint64 handle) const
{
    for (std::size_t i = 0; i < mEntries.size(); ++i)
    {
        if (mEntries[i].handle == static_cast<std::uint64_t>(handle))
            return static_cast<int>(i);
    }
    return -1;
}

void FileListModel::notifySelectionChanged()
{
    emit selectionChanged();
    if (!mEntries.empty())
        emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1), {SelectedRole});
}
