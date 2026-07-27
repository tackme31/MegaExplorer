import QtQuick
import QtQuick.Controls.FluentWinUI3
// HorizontalHeaderView/TableView are Qt Quick Controls types; the style
// import above re-exports them, but this import is kept explicit for
// qmllint/static-tooling clarity (same reasoning as Main.qml's directory
// import comment).
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// Explorer-style detail view for the list-view mode (Phase 6b): a 4-column
// TableView (Name/Modified/Kind/Size). Column header labels are hardcoded
// here rather than sourced from the model's headerData() -- this codebase's
// convention is "C++ passes structured fields, QML composes user-facing
// text" (see NotificationController/ErrorToast.qml), which also sidesteps an
// MSVC codepage gotcha with Japanese literals in .cpp/.h files (see
// src/core/FileKind.h). Sorting is NOT implemented here (deferred to a
// later phase, see MEMO.md) -- the header is a static label row, clicking
// it does nothing.
ColumnLayout {
    id: root
    spacing: 0

    // Re-exposed so Main.qml (which owns `window.activateEntry`) can wire
    // this file's double-click/download-open dispatch without this file
    // needing to know about `window` -- a plain `id` from Main.qml's object
    // tree isn't reachable from a separately-loaded QML file.
    signal activateRequested(bool isFolder, var handle, string name, var sizeBytes)

    // Kind/"種類" display text, composed here from the isFolder/extension
    // roles rather than in C++ -- see the file-level comment above.
    function kindText(isFolder, extension) {
        if (isFolder)
            return qsTr("ファイル フォルダー");
        if (extension === "")
            return qsTr("ファイル");
        return extension + qsTr(" ファイル");
    }

    readonly property var columnLabels: [qsTr("名前"), qsTr("更新日時"), qsTr("種類"), qsTr("サイズ")]

    HorizontalHeaderView {
        id: header
        Layout.fillWidth: true
        syncView: tableView
        clip: true
        // Same rationale as tableView below -- HorizontalHeaderView is also
        // a Flickable and would otherwise let click-drag pan it independently
        // of the synced tableView.
        acceptedButtons: Qt.NoButton

        delegate: Rectangle {
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
                text: root.columnLabels[column]
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

        columnWidthProvider: function (column) {
            const w = explicitColumnWidth(column);
            if (w >= 0)
                return w;
            return [220, 150, 130, 100][column];
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
            required property string extension

            color: "transparent"

            Label {
                anchors.fill: parent
                anchors.margins: 4
                verticalAlignment: Text.AlignVCenter
                horizontalAlignment: cell.column === 3 ? Text.AlignRight : Text.AlignLeft
                elide: Text.ElideMiddle
                text: {
                    switch (cell.column) {
                    case 0:
                        return (cell.isFolder ? "📁 " : "📄 ") + cell.name;
                    case 1:
                        return Qt.formatDateTime(new Date(cell.modificationTime * 1000),
                                                 Locale.ShortFormat);
                    case 2:
                        return root.kindText(cell.isFolder, cell.extension);
                    case 3:
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
