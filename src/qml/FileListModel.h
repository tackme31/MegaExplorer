#pragma once
#include "core/FileEntry.h"

#include <QAbstractTableModel>
#include <QVariantList>

#include <optional>
#include <unordered_set>
#include <vector>

// Owned by main.cpp's composition root and exposed to QML via
// setContextProperty(); not instantiated from QML, so no QML_ELEMENT needed.
//
// QAbstractTableModel (not QAbstractListModel) since Phase 6b: columnCount()
// reports 3 (Name/Modified/Size) so TableView+HorizontalHeaderView can
// render an Explorer-style detail view. data(index, role) still dispatches
// purely on role and ignores index.column() -- ListView/GridView (used by
// the grid/thumbnail view) only ever query column 0 of a multi-column
// model, so that view keeps working unmodified off this same instance.
//
// No headerData() override: column header text is Japanese, and this
// codebase's convention is "C++ passes structured fields, QML composes
// user-facing text" (see NotificationController/ErrorToast.qml) -- also
// sidesteps an MSVC codepage gotcha with non-ASCII literals in .cpp/.h files
// (see FileKind.h). FileTableView.qml's header delegate hardcodes the
// labels instead.
class FileListModel : public QAbstractTableModel
{
    Q_OBJECT
    Q_PROPERTY(QVariantList selectedHandles READ selectedHandlesVariant NOTIFY selectionChanged)

public:
    enum Role
    {
        NameRole = Qt::UserRole + 1,
        SizeRole,
        IsFolderRole,
        HandleRole,
        HasThumbnailRole,
        ThumbnailPathRole,
        ModificationTimeRole,
        FormattedSizeRole,
        SelectedRole,
    };

    explicit FileListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setEntries(std::vector<FileEntry> entries);

    // Updates just the thumbnail-path role for handle's row (no full model
    // reset, unlike setEntries) so a grid view doesn't flicker/relayout when
    // a thumbnail arrives asynchronously. No-op if handle's row is no longer
    // present (e.g. the user navigated away before the fetch completed).
    void setThumbnailPath(quint64 handle, QString path);

    // Click-driven selection, called from the grid/list delegates' TapHandler
    // (row is the model row tapped; modifiers is a Qt::KeyboardModifiers
    // value, passed through as int from QML's TapHandler.point.modifiers).
    // Plain click: replace selection with just this row, move the anchor
    // here. Ctrl: toggle this row in/out of the selection, move the anchor
    // here. Shift: replace selection with the contiguous range between the
    // anchor and this row (falls back to just this row if the anchor's
    // handle is no longer present).
    Q_INVOKABLE void selectRow(int row, int modifiers);
    Q_INVOKABLE void clearSelection();

    QVariantList selectedHandlesVariant() const;

    // Typed accessor for future non-QML consumers (e.g. a delete/move
    // controller), mirroring the fileListModelForThumbnails() typed-accessor
    // precedent in FolderNavigationController.
    const std::unordered_set<quint64>& selectedHandleSet() const
    {
        return mSelectedHandles;
    }

signals:
    void selectionChanged();

private:
    // Drops selected/anchor handles no longer present in mEntries. Handles
    // are globally unique per MEGA node, so this alone is enough to clear
    // the selection on navigation/search (new entries share no handles with
    // the old ones) while preserving it across a same-folder re-sort (same
    // handles, different order) -- no caller-side special-casing needed.
    void pruneSelection();

    std::vector<FileEntry> mEntries;
    // Parallel to mEntries (same index, resized alongside it in setEntries).
    // Kept out of FileEntry itself since it's a session-local, GUI-populated
    // cache result, not SDK domain data -- FileEntry stays Qt-free.
    std::vector<QString> mThumbnailPaths;
    std::unordered_set<quint64> mSelectedHandles;
    std::optional<quint64> mAnchorHandle; // last click anchor, for Shift-range
};
