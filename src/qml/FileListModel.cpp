#include "FileListModel.h"

#include "core/MenuActionResolver.h"

#include <QLocale>
#include <QVariantMap>

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
        case IsFavouriteRole:
            return entry.isFavourite;
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
        {IsFavouriteRole, "isFavourite"},
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
    emit countChanged(); // the only place the row count ever changes

    // Rows the band was measured against are gone; the gesture's remaining
    // updates would address different items, so drop the session and let the
    // selection stand as pruned.
    mBandActive = false;
    mBandBase.clear();
    mBandFirstHandle.reset();
    mBandLastHandle.reset();

    pruneSelection();
}

void FileListModel::setViewKind(ViewKind kind)
{
    if (mViewKind == kind)
        return;
    mViewKind = kind;
    emit selectionChanged(); // availableActions' NOTIFY: the kind is one of its inputs
}

void FileListModel::setCrossFolderListing(bool crossFolder)
{
    if (mCrossFolderListing == crossFolder)
        return;
    mCrossFolderListing = crossFolder;
    emit selectionChanged(); // availableActions' NOTIFY, as in setViewKind above
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

void FileListModel::setFavourite(quint64 handle, bool favourite)
{
    for (std::size_t i = 0; i < mEntries.size(); ++i)
    {
        if (mEntries[i].handle == static_cast<std::uint64_t>(handle))
        {
            mEntries[i].isFavourite = favourite;
            const int row = static_cast<int>(i);
            emit dataChanged(index(row, 0), index(row, columnCount() - 1), {IsFavouriteRole});
            return;
        }
    }
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

SelectionSummary FileListModel::selectionSummary() const
{
    SelectionSummary summary;
    for (const FileEntry& entry : mEntries)
    {
        if (!mSelectedHandles.count(static_cast<quint64>(entry.handle)))
            continue;
        if (entry.isFolder)
            ++summary.folderCount;
        else
            ++summary.fileCount;
    }
    return summary;
}

QStringList FileListModel::availableActions() const
{
    QStringList actions;
    const MenuContext context{mViewKind,
                              MenuSite::FileSelection,
                              selectionSummary(),
                              mCrossFolderListing};
    for (MenuAction action : resolveMenuActions(context))
        actions.append(QString::fromLatin1(menuActionId(action)));
    return actions;
}

QVariantList FileListModel::selectedEntries() const
{
    QVariantList result;
    for (const FileEntry& entry : mEntries)
    {
        if (!mSelectedHandles.count(static_cast<quint64>(entry.handle)))
            continue;
        QVariantMap map;
        map.insert(QStringLiteral("handle"), static_cast<qulonglong>(entry.handle));
        map.insert(QStringLiteral("name"), QString::fromStdString(entry.name));
        map.insert(QStringLiteral("sizeBytes"), static_cast<qulonglong>(entry.sizeBytes));
        map.insert(QStringLiteral("isFolder"), entry.isFolder);
        map.insert(QStringLiteral("isFavourite"), entry.isFavourite);
        result.append(map);
    }
    return result;
}

QVariantMap FileListModel::selectedEntry() const
{
    QVariantMap map;
    if (mSelectedHandles.size() != 1)
        return map;

    const int row = rowForHandle(*mSelectedHandles.begin());
    if (row < 0)
        return map;

    const FileEntry& entry = mEntries[static_cast<size_t>(row)];
    map.insert(QStringLiteral("handle"), static_cast<qulonglong>(entry.handle));
    map.insert(QStringLiteral("name"), QString::fromStdString(entry.name));
    map.insert(QStringLiteral("sizeBytes"), static_cast<qulonglong>(entry.sizeBytes));
    map.insert(QStringLiteral("isFolder"), entry.isFolder);
    map.insert(QStringLiteral("isFavourite"), entry.isFavourite);
    return map;
}

QVariantMap FileListModel::entryAt(int row) const
{
    QVariantMap map;
    if (row < 0 || row >= static_cast<int>(mEntries.size()))
        return map;

    const FileEntry& entry = mEntries[static_cast<size_t>(row)];
    map.insert(QStringLiteral("handle"), static_cast<qulonglong>(entry.handle));
    map.insert(QStringLiteral("name"), QString::fromStdString(entry.name));
    map.insert(QStringLiteral("isFolder"), entry.isFolder);
    return map;
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

int FileListModel::rowForName(const QString& name) const
{
    const std::string needle = name.toStdString();
    for (std::size_t i = 0; i < mEntries.size(); ++i)
    {
        if (mEntries[i].name == needle)
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

void FileListModel::notifySelectionChanged(int firstRow, int lastRow)
{
    emit selectionChanged();
    if (firstRow >= 0 && lastRow >= firstRow)
        emit dataChanged(index(firstRow, 0), index(lastRow, columnCount() - 1), {SelectedRole});
}

void FileListModel::beginBandSelection(bool additive)
{
    mBandActive = true;
    mBandBase = additive ? mSelectedHandles : std::unordered_set<quint64>{};
    mBandFirstHandle.reset();
    mBandLastHandle.reset();
}

void FileListModel::updateBandSelection(int firstRow, int lastRow)
{
    // One column of one item per row is the same block as a list.
    applyBandSelection(firstRow, lastRow, 1, 0, 0);
}

void FileListModel::updateBandSelectionGrid(
    int firstGridRow, int lastGridRow, int columns, int firstColumn, int lastColumn)
{
    applyBandSelection(firstGridRow, lastGridRow, columns, firstColumn, lastColumn);
}

void FileListModel::applyBandSelection(
    int firstGridRow, int lastGridRow, int columns, int firstColumn, int lastColumn)
{
    if (!mBandActive)
        return;

    const int rows = static_cast<int>(mEntries.size());
    std::unordered_set<quint64> next = mBandBase;
    std::optional<quint64> bandFirst;
    std::optional<quint64> bandLast;

    const bool covers = firstGridRow >= 0 && lastGridRow >= firstGridRow && columns > 0 &&
                        firstColumn >= 0 && lastColumn >= firstColumn && rows > 0;
    if (covers)
    {
        // Clamped before multiplying, so a band dragged far past the last row
        // can't overflow gridRow * columns.
        const int lastValidGridRow = (rows - 1) / columns;
        const int loRow = std::min(firstGridRow, lastValidGridRow);
        const int hiRow = std::min(lastGridRow, lastValidGridRow);
        const int hiColumn = std::min(lastColumn, columns - 1);

        for (int gridRow = loRow; gridRow <= hiRow; ++gridRow)
        {
            for (int column = firstColumn; column <= hiColumn; ++column)
            {
                const int row = gridRow * columns + column;
                if (row < 0 || row >= rows)
                    continue;
                const quint64 handle =
                    static_cast<quint64>(mEntries[static_cast<std::size_t>(row)].handle);
                next.insert(handle);
                if (!bandFirst)
                    bandFirst = handle;
                bandLast = handle;
            }
        }
    }

    // Single pass over the rows: builds the repaint range and decides whether
    // anything changed at all, without an extra set comparison.
    int firstChanged = -1;
    int lastChanged = -1;
    for (int row = 0; row < rows; ++row)
    {
        const quint64 handle = static_cast<quint64>(mEntries[static_cast<std::size_t>(row)].handle);
        if (next.count(handle) == mSelectedHandles.count(handle))
            continue;
        if (firstChanged < 0)
            firstChanged = row;
        lastChanged = row;
    }

    mBandFirstHandle = bandFirst;
    mBandLastHandle = bandLast;

    if (firstChanged < 0)
        return; // band moved, but over nothing that flips a row

    mSelectedHandles = std::move(next);
    notifySelectionChanged(firstChanged, lastChanged);
}

void FileListModel::endBandSelection()
{
    if (!mBandActive)
        return;

    mBandActive = false;
    mBandBase.clear();

    // An empty band is the drag equivalent of a click on empty space.
    mAnchorHandle = mBandFirstHandle;
    mCursorHandle = mBandLastHandle;
    mBandFirstHandle.reset();
    mBandLastHandle.reset();
}

void FileListModel::cancelBandSelection()
{
    if (!mBandActive)
        return;

    applyBandSelection(-1, -1, 0, 0, 0); // empty band -> back to mBandBase
    mBandActive = false;
    mBandBase.clear();
    mBandFirstHandle.reset();
    mBandLastHandle.reset();
}
