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
// ThumbnailController (both hold a shared_ptr), and reaching QML only through
// FolderNavigationController's fileListModel property -- not instantiated
// from QML, so no QML_ELEMENT needed.
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
// user-facing text" (see NotificationController/ToastStack.qml) -- also
// sidesteps an MSVC codepage gotcha with non-ASCII literals in .cpp/.h files
// (see FileKind.h). FileTableView.qml's header delegate hardcodes the
// labels instead.
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

    // Selects every row. Anchor/cursor are kept if currently valid, else
    // seeded to row 0 -- mirrors Explorer's Ctrl+A not moving the focus
    // rectangle.
    Q_INVOKABLE void selectAll();

    // Keyboard-driven cursor movement. delta is a view-computed signed row
    // offset (list: +-1 for Up/Down; grid: +-1 for Left/Right, +-columns for
    // Up/Down -- grid geometry is QML-only, so the view does that math).
    // modifiers is a Qt::KeyboardModifiers value, passed through as int like
    // selectRow()'s. No-op if nothing is selected. Target is clamped (not
    // wrapped) to [0, rowCount()-1]. Shift extends the selection from the
    // anchor to the target without moving the anchor; otherwise (Ctrl or no
    // modifier -- Ctrl is treated the same as a plain arrow) the selection
    // collapses to just the target and the anchor moves there too.
    Q_INVOKABLE void moveCursor(int delta, int modifiers);

    // Row of the last-touched item (selectRow/moveCursor/selectAll), or -1
    // if unset. Not a Q_PROPERTY: it changes on every server-side re-sort
    // (setEntries) and nothing binds to it -- QML reads it once, right after
    // calling moveCursor().
    Q_INVOKABLE int cursorRow() const;

    QVariantList selectedHandlesVariant() const;

    // Typed accessor for future non-QML consumers (e.g. a delete/move
    // controller), mirroring the fileListModelForThumbnails() typed-accessor
    // precedent in FolderNavigationController.
    const std::unordered_set<quint64>& selectedHandleSet() const
    {
        return mSelectedHandles;
    }

    // Typed accessor feeding MenuActionResolver -- counts, doesn't collect,
    // since the resolver only needs file/folder tallies.
    SelectionSummary selectionSummary() const;

    // Stable action IDs (see MenuActionResolver::menuActionId) for the
    // MenuSite::FileSelection site, in menu display order. Backs the
    // availableActions Q_PROPERTY that FileContextMenu.qml drives its
    // Instantiator off. The other sites have no model and no change signal,
    // so they go through the MenuActions singleton (src/qml/MenuActions.h)
    // instead.
    QStringList availableActions() const;

    // Row-ordered {handle, name, sizeBytes, isFolder} maps for every selected
    // row -- unlike mSelectedHandles (an unordered_set), callers that act on
    // the selection (e.g. bulk download, or a drag's start-of-gesture
    // snapshot) need a stable, predictable order. Walks mEntries rather than
    // mSelectedHandles for that reason.
    Q_INVOKABLE QVariantList selectedEntries() const;

    // One row's {handle, name, isFolder}, or an empty map when row is out of
    // range. Lets a view-level drag/drop handler ask "what is under the cursor"
    // after resolving a position to a row, which it can't do through data()
    // from QML -- the Role enum above isn't exposed there.
    Q_INVOKABLE QVariantMap entryAt(int row) const;

    // Rubber-band (rectangle) selection, Phase 21. A gesture is a session:
    // begin once on press, update on every move, end (or cancel) on release.
    // The band's covered rows are recomputed from scratch each update, so
    // shrinking the rectangle deselects again -- what the band adds is layered
    // on top of mBandBase, the selection as it was when the drag started
    // (empty unless additive, i.e. Ctrl held).
    Q_INVOKABLE void beginBandSelection(bool additive);

    // List view: the band covers the contiguous rows [firstRow, lastRow].
    // Pass (-1, -1) for a band that currently covers nothing.
    Q_INVOKABLE void updateBandSelection(int firstRow, int lastRow);

    // Grid view: the band covers a rectangular block of the grid, where
    // model row = gridRow * columns + gridColumn. Same (-1, -1) convention.
    Q_INVOKABLE void updateBandSelectionGrid(
        int firstGridRow, int lastGridRow, int columns, int firstColumn, int lastColumn);

    // Ends the session, leaving the selection as it stands and putting the
    // anchor/cursor on the band's first/last covered row -- so a Shift+click
    // right after a band extends from where the band started.
    Q_INVOKABLE void endBandSelection();

    // Ends the session by restoring the selection the drag started from
    // (the DragHandler's onCanceled path).
    Q_INVOKABLE void cancelBandSelection();

signals:
    void selectionChanged();
    void countChanged();

private:
    // Drops selected/anchor handles no longer present in mEntries. Handles
    // are globally unique per MEGA node, so this alone is enough to clear
    // the selection on navigation/search (new entries share no handles with
    // the old ones) while preserving it across a same-folder re-sort (same
    // handles, different order) -- no caller-side special-casing needed.
    void pruneSelection();

    // Row index for handle within mEntries, or -1 if not present.
    int rowForHandle(quint64 handle) const;

    // Common tail of every selection mutator: emit selectionChanged() plus
    // a full-table dataChanged(SelectedRole) so both views repaint.
    void notifySelectionChanged();

    // Same, restricted to rows [firstRow, lastRow]. Band updates run once per
    // mouse move, and repainting every row of a 600k-row folder per frame is
    // exactly what the rectangle's own bookkeeping already avoids.
    void notifySelectionChanged(int firstRow, int lastRow);

    // Shared body of both updateBandSelection* overloads: rebuilds the
    // selection as mBandBase plus the covered block, then emits only over the
    // rows whose selected state actually flipped.
    void applyBandSelection(
        int firstGridRow, int lastGridRow, int columns, int firstColumn, int lastColumn);

    std::vector<FileEntry> mEntries;
    // Parallel to mEntries (same index, resized alongside it in setEntries).
    // Kept out of FileEntry itself since it's a session-local, GUI-populated
    // cache result, not SDK domain data -- FileEntry stays Qt-free.
    std::vector<QString> mThumbnailPaths;
    std::unordered_set<quint64> mSelectedHandles;
    std::optional<quint64> mAnchorHandle; // last click anchor, for Shift-range
    std::optional<quint64> mCursorHandle; // last-touched row, for keyboard nav

    // Rubber-band session state (Phase 21), all meaningless while inactive.
    bool mBandActive = false;
    std::unordered_set<quint64> mBandBase;   // selection at press, for Ctrl+band
    std::optional<quint64> mBandFirstHandle; // first/last row the band covers,
    std::optional<quint64> mBandLastHandle;  // become anchor/cursor on end
};
