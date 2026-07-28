import QtQuick
import QtQuick.Controls.FluentWinUI3
// HorizontalHeaderView/TableView are Qt Quick Controls types; the style
// import above re-exports them, but this import is kept explicit for
// qmllint/static-tooling clarity (same reasoning as Main.qml's directory
// import comment).
import QtQuick.Controls
import QtQuick.Layouts
import QtCore
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
// persisted via Settings and forwarded to controller.setSortOrder(), which
// re-fetches from the SDK with the matching order rather than sorting
// in-memory.
ColumnLayout {
    id: root
    spacing: 0

    // Re-exposed so Main.qml (which owns `window.activateEntry`) can wire
    // this file's double-click/download-open dispatch without this file
    // needing to know about `window` -- a plain `id` from Main.qml's object
    // tree isn't reachable from a separately-loaded QML file.
    signal activateRequested(bool isFolder, var handle, string name, var sizeBytes)

    readonly property var columnLabels: [qsTr("Name"), qsTr("Date modified"), qsTr("Size")]

    // Arbitrary picks, not derived from any content measurement -- just
    // enough to keep a dragged-in column from shrinking to unreadable/
    // zero-width. Tune by feel later if these turn out wrong.
    readonly property var minColumnWidths: [100, 100, 60]

    // 0 = Name, 1 = Modified, 2 = Size -- matches FileListModel::columnCount()
    // and FolderNavigationController::setSortOrder()'s column mapping.
    property int sortColumn: 0
    property bool sortAscending: true

    // -1 = user has never explicitly resized this column (unset). In that
    // case columnWidthProvider below falls back to its hardcoded
    // [220, 150, 100]. real to match TableView.setColumnWidth()/
    // explicitColumnWidth()'s numeric type.
    property real columnWidthName: -1
    property real columnWidthModified: -1
    property real columnWidthSize: -1

    Settings {
        property alias sortColumn: root.sortColumn
        property alias sortAscending: root.sortAscending
        property alias columnWidthName: root.columnWidthName
        property alias columnWidthModified: root.columnWidthModified
        property alias columnWidthSize: root.columnWidthSize
    }

    // Same column same click: toggle direction. Different column: switch to
    // it, reset to ascending (Explorer's convention).
    function requestSort(column) {
        if (root.sortColumn === column)
            root.sortAscending = !root.sortAscending;
        else {
            root.sortColumn = column;
            root.sortAscending = true;
        }
        controller.setSortOrder(root.sortColumn, root.sortAscending);
    }

    // requestSort()'s counterpart for column widths. TableView has no
    // dedicated resize-finished signal, so layoutChanged (fired from
    // TableView below) is the only official hook -- but it also fires on
    // scrolling, unrelated to width. Safe to call unconditionally: a QML
    // property assignment that doesn't change the value is a no-op, so no
    // spurious Settings writes happen. The >= 0 guard (explicitColumnWidth
    // returns -1 when unset) also protects against a layoutChanged firing
    // before Component.onCompleted has restored persisted widths from
    // clobbering them with -1 -- nothing in this UI ever explicitly resets a
    // column back to unset.
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

    // Settings restoration above runs before this fires, so the initial
    // fetch (once loadRoot() has actually happened -- see
    // FolderNavigationController::mHasLoadedOnce) uses the persisted order.
    Component.onCompleted: {
        controller.setSortOrder(root.sortColumn, root.sortAscending);
        root.restoreColumnWidths();
    }

    HorizontalHeaderView {
        id: header
        Layout.fillWidth: true
        syncView: tableView
        clip: true
        // textRole defaults to "display", which FileListModel::roleNames()
        // doesn't provide (this delegate below builds header text itself
        // from columnLabels, never via textRole) -- left unset it just spams
        // a "role doesn't exist" warning on every load. Qt docs: setting
        // textRole silences that check.
        textRole: ""
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
        model: controller.fileListModel
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

        delegate: Rectangle {
            id: cell
            implicitHeight: 28
            required property int column
            required property string name
            required property bool isFolder
            required property var handle
            required property var sizeBytes
            required property string formattedSize
            required property double modificationTime

            color: "transparent"

            Label {
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
                                                                          * 1000), Locale.ShortFormat);
                    case 2:
                        return cell.formattedSize;
                    }
                    return "";
                }
            }

            TapHandler {
                acceptedButtons: Qt.LeftButton
                onDoubleTapped: root.activateRequested(cell.isFolder, cell.handle, cell.name,
                                                       cell.sizeBytes)
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: if (!cell.isFolder)
                              contextMenu.popup()
            }

            FileContextMenu {
                id: contextMenu
                delegateItem: cell
            }
        }
    }
}
