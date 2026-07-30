import QtQuick
import QtQuick.Controls.FluentWinUI3
// HorizontalHeaderView/TableView are Qt Quick Controls types; the style
// import above re-exports them, but this import is kept explicit for
// qmllint/static-tooling clarity (same reasoning as Main.qml's directory
// import comment).
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// Explorer-style detail view for the list-view mode (Phase 6b/sort): a
// 3-column TableView (Name/Modified/Size -- a "Kind" column was considered
// but dropped, see MEMO.md's 2026-07-28 note: MegaApi::getChildren/search
// has no order value corresponding to it, and sorting here is deliberately
// server-side, not in-memory). Column header labels are hardcoded here
// rather than sourced from the model's headerData() -- this codebase's
// convention is "C++ passes structured fields, QML composes user-facing
// text" (see NotificationController/ErrorToast.qml), which also sidesteps an
// MSVC codepage gotcha with Japanese literals in .cpp/.h files.
//
// Clicking a header sorts by that column (first click ascending, repeat
// click toggles direction, same as Explorer); the chosen column/direction is
// forwarded to navController.setSortOrder(), which re-fetches from the SDK
// with the matching order rather than sorting in-memory.
//
// Sort order and column widths are persisted app-wide (last-write-wins
// across tabs, see TabContentPane.qml) rather than via a Settings item in
// this file directly -- Phase 9 made this a per-tab view (one live instance
// per open tab), and N tabs each owning a Settings item would fight over the
// same keys. initialSortColumn/initialSortAscending/initialColumnWidth*
// (required properties, set by TabContentPane from its own initial* values)
// seed this tab's starting point; sortOrderChanged/columnWidthsChanged relay
// this tab's own changes back out so TabContentPane can write them through
// to the single Settings instance (Main.qml).
ColumnLayout {
    id: root
    spacing: 0

    required property var navController

    // Re-exposed so TabContentPane (which owns activation/download dispatch)
    // can wire this file's double-click/download-open dispatch without this
    // file needing to know about that -- a plain `id` from a separately-
    // loaded QML file's object tree isn't reachable from here.
    signal activateRequested(bool isFolder, var handle, string name, var sizeBytes)
    // Middle-click on a folder row below -- ignored for files, same
    // restriction as the "Open in new tab" context-menu action
    // (FileActionResolver's FoldersOnly/SingleOnly spec).
    signal openInNewTabRequested(var handle)

    // See this file's own top comment: relayed up to TabContentPane/Main.qml
    // rather than written to a local Settings item.
    signal sortOrderChanged(int column, bool ascending)
    signal columnWidthsChanged(real nameWidth, real modifiedWidth, real sizeWidth)

    required property int initialSortColumn
    required property bool initialSortAscending
    required property real initialColumnWidthName
    required property real initialColumnWidthModified
    required property real initialColumnWidthSize

    readonly property var columnLabels: [qsTr("Name"), qsTr("Date modified"), qsTr("Size")]

    SystemPalette {
        id: sysPalette
    }

    // Matches (English) Windows Explorer's Date modified column: short date
    // + short time, e.g. "7/28/2026 3:45 PM". Not reproducible via
    // Qt.formatDateTime(date, Locale.ShortFormat) -- QLocale's own
    // ShortFormat for en_US is "M/d/yy h:mm AP" (2-digit year), while
    // Explorer's regional short-date default is 4-digit ("M/d/yyyy").
    // "AP" is Qt's AM/PM token (its "tt" is a timezone token, not AM/PM --
    // easy mix-up with .NET/Windows format-string syntax, where tt does
    // mean AM/PM).
    //
    // Wrapped in qsTr(), same as columnLabels above, so a Japanese .ts file
    // can later supply the equivalent Japanese Explorer format (typically
    // 24-hour, no AM/PM -- e.g. "yyyy/MM/dd H:mm") without any change here.
    readonly property string modifiedDateFormat: qsTr("M/d/yyyy h:mm AP")

    // Arbitrary picks, not derived from any content measurement -- just
    // enough to keep a dragged-in column from shrinking to unreadable/
    // zero-width. Tune by feel later if these turn out wrong.
    readonly property var minColumnWidths: [100, 100, 60]

    // 0 = Name, 1 = Modified, 2 = Size -- matches FileListModel::columnCount()
    // and FolderNavigationController::setSortOrder()'s column mapping.
    // Plain literal defaults (not bound to initialSortColumn/initialColumnWidth*
    // below) -- Component.onCompleted assigns the real starting value exactly
    // once, deliberately as a one-time imperative copy rather than a live
    // binding, so this tab's value doesn't keep tracking a sibling tab's
    // later changes to the shared Settings-backed value (see this file's top
    // comment and TabContentPane.qml's matching viewMode comment).
    property int sortColumn: 0
    property bool sortAscending: true

    // -1 = user has never explicitly resized this column (unset). In that
    // case columnWidthProvider below falls back to its hardcoded
    // [220, 150, 100]. real to match TableView.setColumnWidth()/
    // explicitColumnWidth()'s numeric type.
    property real columnWidthName: -1
    property real columnWidthModified: -1
    property real columnWidthSize: -1

    // Relays this tab's own sort-order/column-width changes back out (see
    // this file's top comment). Also fires once during Component.onCompleted
    // below when the initial* values differ from the literal defaults above
    // -- a harmless, idempotent echo of the value this tab just read.
    onSortColumnChanged: root.sortOrderChanged(root.sortColumn, root.sortAscending)
    onSortAscendingChanged: root.sortOrderChanged(root.sortColumn, root.sortAscending)
    onColumnWidthNameChanged: root.columnWidthsChanged(root.columnWidthName,
                                                       root.columnWidthModified,
                                                       root.columnWidthSize)
    onColumnWidthModifiedChanged: root.columnWidthsChanged(root.columnWidthName,
                                                           root.columnWidthModified,
                                                           root.columnWidthSize)
    onColumnWidthSizeChanged: root.columnWidthsChanged(root.columnWidthName,
                                                       root.columnWidthModified,
                                                       root.columnWidthSize)

    // Same column same click: toggle direction. Different column: switch to
    // it, reset to ascending (Explorer's convention).
    function requestSort(column) {
        if (root.sortColumn === column)
            root.sortAscending = !root.sortAscending;
        else {
            root.sortColumn = column;
            root.sortAscending = true;
        }
        root.navController.setSortOrder(root.sortColumn, root.sortAscending);
    }

    // requestSort()'s counterpart for column widths. TableView has no
    // dedicated resize-finished signal, so layoutChanged (fired from
    // TableView below) is the only official hook -- but it also fires on
    // scrolling, unrelated to width. Safe to call unconditionally: a QML
    // property assignment that doesn't change the value is a no-op, so no
    // spurious writes happen. The >= 0 guard (explicitColumnWidth returns -1
    // when unset) also protects against a layoutChanged firing before
    // Component.onCompleted has restored persisted widths from clobbering
    // them with -1 -- nothing in this UI ever explicitly resets a column
    // back to unset.
    function saveColumnWidths() {
        const w0 = tableView.explicitColumnWidth(0);
        const w1 = tableView.explicitColumnWidth(1);
        const w2 = tableView.explicitColumnWidth(2);
        // Clamped to minColumnWidths here too (not just in
        // columnWidthProvider) so the persisted value already reflects what
        // the user actually sees, rather than the raw, possibly
        // sub-minimum, drag position.
        if (w0 >= 0)
            root.columnWidthName = Math.max(root.minColumnWidths[0], w0);
        if (w1 >= 0)
            root.columnWidthModified = Math.max(root.minColumnWidths[1], w1);
        if (w2 >= 0)
            root.columnWidthSize = Math.max(root.minColumnWidths[2], w2);
    }

    // Only sets a column's explicit width when a value was actually
    // persisted (>= 0) -- passing the -1 "unset" sentinel to
    // setColumnWidth() resets the column to content-based auto-width,
    // silently overriding columnWidthProvider's [220, 150, 100] fallback for
    // fresh installs.
    //
    // Called both from Component.onCompleted below and from
    // TableView.onRowsChanged: FileListModel::setEntries() (the initial
    // post-login folder load, and every subsequent folder navigation) does a
    // full beginResetModel()/endResetModel(), and TableView does not
    // guarantee it keeps explicitly-set column widths across a model reset.
    // Without reapplying on the rows-changed callback, a freshly logged-in
    // session would show the hardcoded default widths (not the persisted
    // ones) until the user resized a column again.
    function restoreColumnWidths() {
        if (root.columnWidthName >= 0)
            tableView.setColumnWidth(0, root.columnWidthName);
        if (root.columnWidthModified >= 0)
            tableView.setColumnWidth(1, root.columnWidthModified);
        if (root.columnWidthSize >= 0)
            tableView.setColumnWidth(2, root.columnWidthSize);
    }

    // The initial* required properties (set by TabContentPane from the
    // single shared Settings instance) are copied in here -- assignment
    // first, then navController.setSortOrder()/restoreColumnWidths() so the
    // initial fetch (once loadRoot() has actually happened -- see
    // FolderNavigationController::mHasLoadedOnce) uses the restored order.
    Component.onCompleted: {
        root.sortColumn = root.initialSortColumn;
        root.sortAscending = root.initialSortAscending;
        root.columnWidthName = root.initialColumnWidthName;
        root.columnWidthModified = root.initialColumnWidthModified;
        root.columnWidthSize = root.initialColumnWidthSize;
        root.navController.setSortOrder(root.sortColumn, root.sortAscending);
        root.restoreColumnWidths();
    }

    // Handle of the row currently being renamed in place, 0 when not renaming
    // (same meaningless-sentinel convention as PathSegment::isRoot's handle).
    // Only the name cell of that one row swaps its Label for an
    // InlineRenameField, via a Loader -- see the delegate below.
    property var renamingHandle: 0

    // Shared by F2 and the context menu's renameRequested. Renaming is
    // inherently single-item, so this collapses the selection to the cursor row
    // first -- selectRow(row, Qt.NoModifier) already means "deselect
    // everything else and select just this row", so FileListModel needed
    // nothing new. After a right-click the cursor is already on the clicked
    // row (the delegate's right-button TapHandler selects it), so both entry
    // points land here identically.
    function beginRename() {
        if (root.renamingHandle !== 0)
            return;
        const model = root.navController.fileListModel;
        const row = model.cursorRow();
        if (row < 0)
            return;
        model.selectRow(row, Qt.NoModifier);
        const entries = model.selectedEntries();
        if (entries.length !== 1)
            return;
        root.renamingHandle = entries[0].handle;
        tableView.positionViewAtRow(row, TableView.Contain);
    }

    // Focus must be handed back explicitly, same reason the focusPolicy:
    // Qt.NoFocus assignments elsewhere exist -- otherwise arrow keys go dead
    // once the field is gone.
    function endRename() {
        root.renamingHandle = 0;
        root.forceActiveFocus();
    }

    // Called from the field's committed signal, so tearing the field down is
    // deferred past the end of that signal's own handler.
    function commitRename(handle, oldName, newName) {
        if (newName !== oldName)
            root.navController.renameEntry(handle, newName);
        Qt.callLater(root.endRename);
    }

    // Placed on root (not tableView) so Main.qml has a single forceActiveFocus()
    // target per view, regardless of which child actually holds activeFocus.
    // Ctrl+A is handled here rather than via a window-level Shortcut so it
    // doesn't fire while the header search TextField has focus (Shortcut
    // ignores focus entirely and would steal Ctrl+A from text selection);
    // Keys.onPressed only fires for the item that currently has activeFocus.
    Keys.onPressed: event => {
        if (event.modifiers & Qt.AltModifier)
            return; // reserved for a future Alt+Left "back" shortcut

        // While the rename field has focus this is still on its key-propagation
        // path, and it doesn't consume F2/Delete the way it consumes arrows and
        // Ctrl+A -- so the view has to stand down explicitly.
        if (root.renamingHandle !== 0)
            return;

        if (event.key === Qt.Key_F2) {
            root.beginRename();
            event.accepted = true;
            return;
        }

        if (event.key === Qt.Key_Delete) {
            confirmRubbishDialog.confirm();
            event.accepted = true;
            return;
        }

        if (event.matches(StandardKey.SelectAll)) {
            root.navController.fileListModel.selectAll();
            event.accepted = true;
            return;
        }

        let delta = 0;
        if (event.key === Qt.Key_Up)
            delta = -1;
        else if (event.key === Qt.Key_Down)
            delta = 1;
        else
            return;

        root.navController.fileListModel.moveCursor(delta, event.modifiers);
        const row = root.navController.fileListModel.cursorRow();
        if (row >= 0)
            tableView.positionViewAtRow(row, TableView.Contain);
        event.accepted = true;
    }

    HorizontalHeaderView {
        id: header
        Layout.fillWidth: true
        syncView: tableView
        clip: true
        // textRole defaults to "display", which FileListModel::roleNames()
        // doesn't provide (this delegate below builds header text itself
        // from columnLabels, never via textRole) -- left unset it just spams
        // a "role doesn't exist" warning on every load. Qt treats "" as still
        // unset (it falls back to "display" internally for a
        // QAbstractItemModel), so silencing the warning needs an actual
        // existing role name, not just any explicit assignment -- "name" is
        // never read by the delegate below, it's only here to satisfy Qt's
        // roleNames() existence check.
        textRole: "name"
        // Same rationale as tableView below -- HorizontalHeaderView is also
        // a Flickable and would otherwise let click-drag pan it independently
        // of the synced tableView.
        acceptedButtons: Qt.NoButton

        delegate: Rectangle {
            id: headerCell
            implicitHeight: 32
            required property int column

            // FluentWinUI3 ships no HorizontalHeaderView delegate of its own
            // (unlike Basic), so this Rectangle's background is the only one
            // drawn here -- leaving `color` at Rectangle's default (opaque
            // white) clashed with the Label's palette-driven (theme-follows-
            // Windows) foreground, producing invisible white-on-white text.
            // transparent lets the real themed background show through,
            // same as the row delegate below.
            color: "transparent"

            Label {
                anchors.fill: parent
                anchors.margins: 6
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                text: root.columnLabels[headerCell.column] + (root.sortColumn === headerCell.column
                                                              ? (root.sortAscending ? " ▲" : " ▼") :
                                                                "")
            }

            // Same TapHandler-on-a-child pattern as the row delegate below --
            // the Flickable's own acceptedButtons: Qt.NoButton (above) only
            // suppresses click-drag panning, not delivery to child handlers.
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: root.requestSort(headerCell.column)
            }
        }
    }

    TableView {
        id: tableView
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        resizableColumns: true
        // Flickable (TableView's base) defaults acceptedButtons to
        // Qt.LeftButton, i.e. click-drag pans the view -- unexpected for an
        // Explorer-style list. NoButton disables drag/flick while leaving
        // wheel scrolling untouched (Flickable.acceptedButtons, since 6.9).
        acceptedButtons: Qt.NoButton
        // Defensive: TableView's built-in key navigation needs a
        // selectionModel to do anything, which this view doesn't set, so
        // this is currently a no-op -- but explicit in case that changes.
        keyNavigationEnabled: false
        model: root.navController.fileListModel
        onLayoutChanged: root.saveColumnWidths()
        // rows changes on every FileListModel::setEntries() reset (initial
        // post-login load and folder navigation) -- see
        // restoreColumnWidths()'s comment for why explicit widths need
        // reapplying here, not just once at Component.onCompleted.
        onRowsChanged: root.restoreColumnWidths()

        columnWidthProvider: function (column) {
            const w = explicitColumnWidth(column);
            // Math.max(min, w) pattern straight from Qt's own TableView docs
            // ("Row heights and column widths") for clamping a
            // user-resizable column to a floor.
            if (w >= 0)
                return Math.max(root.minColumnWidths[column], w);
            return [220, 150, 100][column];
        }

        // The handler is declared inside TableView, which would install it on the
        // contentItem (Qt docs, TableView::cellAtPosition) -- i.e. it would never see
        // taps below the last row, since contentItem is only as tall as the content.
        // parent: tableView scopes it to the viewport instead, at the cost of having
        // to map the tap into content coordinates by hand.
        //
        // It also does the row selection, not just the clearing. The default
        // gesturePolicy (DragThreshold) takes a passive grab only, so a per-cell
        // TapHandler would fire in addition to this one and Ctrl+click would toggle
        // the same row twice, cancelling itself out.
        TapHandler {
            parent: tableView
            acceptedButtons: Qt.LeftButton
            onTapped: {
                const pos = tableView.contentItem.mapFromItem(tableView, point.position);
                // x clamped inside the last column so a tap to its right still hits
                // the row, matching Explorer's full-row selection.
                const hit = tableView.cellAtPosition(Qt.point(Math.min(pos.x, tableView.contentWidth
                                                                       - 1), pos.y), false);
                // This handler only takes a passive grab, so it also fires for
                // taps that land inside the active rename field -- and
                // forceActiveFocus() below would then commit the edit on the
                // user's first click into their own text. The renamed row is the
                // cursor row by construction (see beginRename), so that's the
                // one to stand down for; a tap on any other row falls through
                // and commits via focus loss, which is the wanted behavior.
                if (root.renamingHandle !== 0
                        && hit.y === root.navController.fileListModel.cursorRow())
                    return;
                root.forceActiveFocus();
                if (hit.y < 0)
                    root.navController.fileListModel.clearSelection();
                else
                    root.navController.fileListModel.selectRow(hit.y, point.modifiers);
            }
        }

        delegate: Rectangle {
            id: cell
            implicitHeight: 28
            required property int row
            required property int column
            required property string name
            required property bool isFolder
            required property var handle
            required property var sizeBytes
            required property string formattedSize
            required property double modificationTime
            required property bool selected

            // Only the name column of the one renamed row turns into an editor.
            readonly property bool renaming: root.renamingHandle !== 0
                                             && root.renamingHandle === cell.handle
                                             && cell.column === 0

            color: cell.selected ? Qt.rgba(sysPalette.highlight.r, sysPalette.highlight.g,
                                           sysPalette.highlight.b, 0.35) : "transparent"

            Label {
                visible: !cell.renaming
                anchors.fill: parent
                anchors.margins: 4
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: cell.column === 2 ? Text.AlignRight : Text.AlignLeft
                elide: Text.ElideMiddle
                text: {
                    switch (cell.column) {
                    case 0:
                        return (cell.isFolder ? "📁 " : "📄 ") + cell.name;
                    case 1:
                        // Folders have no modification time from the SDK
                        // (MegaNode::getModificationTime() returns 0 for
                        // them) -- formatting that would show the Epoch
                        // instead of a blank cell, unlike Explorer.
                        return cell.isFolder ? "" : Qt.formatDateTime(new Date(
                                                                          cell.modificationTime
                                                                          * 1000), root.modifiedDateFormat);
                    case 2:
                        return cell.formattedSize;
                    }
                    return "";
                }
            }

            // The Component is declared inline rather than shared at root level
            // because InlineRenameField's properties are `required`, and a
            // Loader has no way to supply those to a component it didn't
            // declare -- inline, they can just bind to `cell`. Component itself
            // instantiates nothing until active turns true.
            Loader {
                anchors.fill: parent
                anchors.margins: 2
                active: cell.renaming
                sourceComponent: Component {
                    InlineRenameField {
                        originalName: cell.name
                        isFolder: cell.isFolder
                        onCommitted: newName => root.commitRename(cell.handle, cell.name, newName)
                        onCancelled: Qt.callLater(root.endRename)
                    }
                }
            }

            // Left-click selection is handled entirely by the view-level
            // background TapHandler above (see its comment) -- this one is
            // double-click-only.
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onDoubleTapped: root.activateRequested(cell.isFolder, cell.handle, cell.name,
                                                       cell.sizeBytes)
            }

            // Folder-only, mirrors FileGridView.qml's delegate -- a file has
            // nothing sensible to open "in a new tab".
            TapHandler {
                acceptedButtons: Qt.MiddleButton
                onTapped: if (cell.isFolder)
                              root.openInNewTabRequested(cell.handle)
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: {
                    if (!cell.selected) {
                        root.forceActiveFocus();
                        root.navController.fileListModel.selectRow(cell.row, Qt.NoModifier);
                    }
                    contextMenu.popup();
                }
            }
        }
    }

    // Selection-driven, one instance for the whole view rather than one per
    // delegate cell (see FileContextMenu.qml's own comment) -- Menu is a
    // Popup, not an Item, so it's neither laid out by this ColumnLayout nor
    // clipped by TableView's Flickable viewport; a parentless popup() opens
    // at the mouse cursor regardless.
    FileContextMenu {
        id: contextMenu
        navController: root.navController
        onRenameRequested: root.beginRename()
        onMoveToRubbishRequested: confirmRubbishDialog.confirm()
    }

    // One per view, same reasoning as the menu above -- the action always
    // targets this view's own selection.
    ConfirmRubbishDialog {
        id: confirmRubbishDialog
        navController: root.navController
    }
}
