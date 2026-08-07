#pragma once
#include "core/FileEntry.h"
#include "core/MenuAction.h"

#include <QAbstractTableModel>
#include <QStringList>
#include <QVariantList>

#include <optional>
#include <unordered_set>
#include <vector>

// One per tab, owned jointly by that tab's FolderNavigationController and
// ThumbnailController, and reaching QML only through the former's fileListModel
// property -- never instantiated from QML, so no QML_ELEMENT.
//
// A table model rather than a list model so TableView+HorizontalHeaderView can
// render the Explorer-style detail view. data() still dispatches purely on role
// and ignores index.column(): ListView/GridView only ever query column 0, so the
// grid view keeps working off this same instance.
//
// No headerData() override: header text is Japanese, and the convention here is
// that C++ passes structured fields while QML composes user-facing text -- which
// also sidesteps the MSVC codepage gotcha with non-ASCII literals (FileKind.h).
class FileListModel : public QAbstractTableModel
{
    Q_OBJECT
    Q_PROPERTY(QVariantList selectedHandles READ selectedHandlesVariant NOTIFY selectionChanged)
    Q_PROPERTY(QStringList availableActions READ availableActions NOTIFY selectionChanged)
    // rowCount() is Q_INVOKABLE on its own, but has no NOTIFY, so a QML
    // binding on it would never re-evaluate. The status bar needs one.
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

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

    // Updates one role for one row, rather than resetting the model, so the grid
    // doesn't relayout when a thumbnail arrives. No-op if the row is gone.
    void setThumbnailPath(quint64 handle, QString path);

    // modifiers is a Qt::KeyboardModifiers value, passed as int from QML. Plain
    // click replaces the selection and moves the anchor; Ctrl toggles this row and
    // moves the anchor; Shift takes the range from the anchor, falling back to this
    // row alone when the anchor's handle is gone.
    Q_INVOKABLE void selectRow(int row, int modifiers);
    Q_INVOKABLE void clearSelection();

    // Keeps a valid anchor/cursor, else seeds row 0 -- Explorer's Ctrl+A doesn't
    // move the focus rectangle either.
    Q_INVOKABLE void selectAll();

    // delta is a view-computed row offset -- grid geometry is QML-only, so the view
    // does that math. The target is clamped, not wrapped. Shift extends from the
    // anchor without moving it; anything else (Ctrl included) collapses the
    // selection to the target and moves the anchor there.
    Q_INVOKABLE void moveCursor(int delta, int modifiers);

    // Row of the last-touched item, or -1. Not a Q_PROPERTY: it changes on every
    // re-sort and QML only reads it right after moveCursor().
    Q_INVOKABLE int cursorRow() const;

    QVariantList selectedHandlesVariant() const;

    const std::unordered_set<quint64>& selectedHandleSet() const
    {
        return mSelectedHandles;
    }

    // Counts rather than collects -- MenuActionResolver only needs the tallies.
    SelectionSummary selectionSummary() const;

    // Stable action IDs for the MenuSite::FileSelection site, in display order. The
    // other sites have no model and no change signal, so they go through the
    // MenuActions singleton instead.
    QStringList availableActions() const;

    // Row-ordered {handle, name, sizeBytes, isFolder} maps. Walks mEntries, not the
    // unordered mSelectedHandles: callers acting on the selection (bulk download, a
    // drag snapshot) need a stable order.
    Q_INVOKABLE QVariantList selectedEntries() const;

    // One row's {handle, name, isFolder}, empty when out of range. Drag/drop
    // handlers can't reach data() from QML -- the Role enum isn't exposed there.
    Q_INVOKABLE QVariantMap entryAt(int row) const;

    // Rubber-band selection is a session: begin on press, update per move, end or
    // cancel on release. Covered rows are recomputed from scratch each update, so
    // shrinking the rectangle deselects again; what the band adds is layered over
    // mBandBase, the selection as it was at press (empty unless additive).
    Q_INVOKABLE void beginBandSelection(bool additive);

    // List view: the band covers the contiguous rows [firstRow, lastRow].
    // Pass (-1, -1) for a band that currently covers nothing.
    Q_INVOKABLE void updateBandSelection(int firstRow, int lastRow);

    // Grid view: the band covers a rectangular block of the grid, where
    // model row = gridRow * columns + gridColumn. Same (-1, -1) convention.
    Q_INVOKABLE void updateBandSelectionGrid(
        int firstGridRow, int lastGridRow, int columns, int firstColumn, int lastColumn);

    // Leaves the selection as it stands and puts the anchor/cursor on the band's
    // first/last row, so a Shift+click afterwards extends from where the band began.
    Q_INVOKABLE void endBandSelection();

    // Restores the selection the drag started from (DragHandler's onCanceled).
    Q_INVOKABLE void cancelBandSelection();

signals:
    void selectionChanged();
    void countChanged();

private:
    // Drops selected/anchor handles no longer in mEntries. Handles are globally
    // unique per node, so this alone clears the selection on navigation while
    // preserving it across a same-folder re-sort -- no caller-side special-casing.
    void pruneSelection();

    // Row index for handle within mEntries, or -1 if not present.
    int rowForHandle(quint64 handle) const;

    // Common tail of every selection mutator: selectionChanged() plus a full-table
    // dataChanged(SelectedRole) so both views repaint.
    void notifySelectionChanged();

    // Same, restricted to [firstRow, lastRow]: band updates run once per mouse move,
    // and repainting every row of a 600k-row folder per frame would be pointless.
    void notifySelectionChanged(int firstRow, int lastRow);

    // Shared body of both updateBandSelection* overloads; emits only over the rows
    // whose selected state actually flipped.
    void applyBandSelection(
        int firstGridRow, int lastGridRow, int columns, int firstColumn, int lastColumn);

    std::vector<FileEntry> mEntries;
    // Parallel to mEntries. Kept out of FileEntry so that stays Qt-free SDK domain
    // data, not a session-local GUI cache.
    std::vector<QString> mThumbnailPaths;
    std::unordered_set<quint64> mSelectedHandles;
    std::optional<quint64> mAnchorHandle; // last click anchor, for Shift-range
    std::optional<quint64> mCursorHandle; // last-touched row, for keyboard nav

    // Rubber-band session state, all meaningless while inactive.
    bool mBandActive = false;
    std::unordered_set<quint64> mBandBase;   // selection at press, for Ctrl+band
    std::optional<quint64> mBandFirstHandle; // first/last row the band covers,
    std::optional<quint64> mBandLastHandle;  // become anchor/cursor on end
};
