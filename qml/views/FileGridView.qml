import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// Thumbnail-grid view for the grid-view mode -- extracted from Main.qml's
// central StackLayout at Phase 9 so TabContentPane.qml (one per tab) can
// instantiate an independent GridView per tab, same as FileTableView.qml's
// TableView already was its own file. Behavior is unchanged from the
// original inline GridView; only the controller/thumbnailController context
// properties (and window.activateEntry()) became navController/
// thumbController required properties plus activateRequested()/
// openInNewTabRequested() signals, since a tab's controllers are no longer
// singletons reachable by a fixed context-property name.
GridView {
    id: root

    required property var navController
    required property var thumbController

    signal activateRequested(bool isFolder, var handle, string name, var sizeBytes)
    // Middle-click on a folder delegate below -- ignored for files, same
    // restriction as the "Open in new tab" context-menu action
    // (FileActionResolver's FoldersOnly/SingleOnly spec).
    signal openInNewTabRequested(var handle)

    model: root.navController.fileListModel
    clip: true
    cellWidth: 120
    cellHeight: 120
    // GridView has its own built-in arrow-key handling (currentIndex
    // movement + auto-scroll) that would otherwise fight with the selection
    // model driven by Keys.onPressed below.
    keyNavigationEnabled: false

    // Rename state and its three entry-point helpers, identical in behavior to
    // FileTableView.qml's -- see the comments there for the reasoning behind
    // each (this view only differs in using positionViewAtIndex).
    property var renamingHandle: 0

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
        root.positionViewAtIndex(row, GridView.Contain);
    }

    function endRename() {
        root.renamingHandle = 0;
        root.forceActiveFocus();
    }

    function commitRename(handle, oldName, newName) {
        if (newName !== oldName)
            root.navController.renameEntry(handle, newName);
        Qt.callLater(root.endRename);
    }

    Keys.onPressed: event => {
        if (event.modifiers & Qt.AltModifier)
            return; // reserved for a future Alt+Left "back" shortcut

        // See FileTableView.qml's matching guard: the rename field doesn't
        // consume F2/Delete, so this view has to stand down while it's up.
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

        // Matches GridView's own FlowLeftToRight layout math; no ScrollBar
        // is attached, so width is the full viewport width. Recomputed per
        // key press rather than cached, so a window resize doesn't need
        // separate handling.
        const columns = Math.max(1, Math.floor(root.width / root.cellWidth));
        let delta = 0;
        if (event.key === Qt.Key_Left)
            delta = -1;
        else if (event.key === Qt.Key_Right)
            delta = 1;
        else if (event.key === Qt.Key_Up)
            delta = -columns;
        else if (event.key === Qt.Key_Down)
            delta = columns;
        else
            return;

        root.navController.fileListModel.moveCursor(delta, event.modifiers);
        const row = root.navController.fileListModel.cursorRow();
        if (row >= 0)
            root.positionViewAtIndex(row, GridView.Contain);
        event.accepted = true;
    }

    SystemPalette {
        id: sysPalette
    }

    // Selection-driven, one instance for the whole view rather than one per
    // delegate item (see FileContextMenu.qml's own comment) -- Menu is a
    // Popup, not an Item, so it's neither laid out by GridView nor clipped
    // by its Flickable viewport; a parentless popup() opens at the mouse
    // cursor regardless.
    FileContextMenu {
        id: gridContextMenu
        navController: root.navController
        onRenameRequested: root.beginRename()
        onMoveToRubbishRequested: confirmRubbishDialog.confirm()
    }

    ConfirmRubbishDialog {
        id: confirmRubbishDialog
        navController: root.navController
    }

    // Same rationale as FileTableView.qml's -- see the comment there for why
    // this handler is re-parented to the view and why it owns the selection.
    TapHandler {
        parent: root
        acceptedButtons: Qt.LeftButton
        onTapped: {
            const pos = root.contentItem.mapFromItem(root, point.position);
            const idx = root.indexAt(pos.x, pos.y);
            // Passive grab, so this also fires for taps inside the active
            // rename field -- see FileTableView.qml's matching guard.
            if (root.renamingHandle !== 0
                    && idx === root.navController.fileListModel.cursorRow())
                return;
            root.forceActiveFocus();
            if (idx < 0)
                root.navController.fileListModel.clearSelection();
            else
                root.navController.fileListModel.selectRow(idx, point.modifiers);
        }
    }

    delegate: Item {
        id: gridDelegateItem
        required property int index
        required property string name
        required property bool isFolder
        required property var handle
        required property var sizeBytes
        required property bool hasThumbnail
        required property string thumbnailPath
        required property bool selected

        readonly property bool renaming: root.renamingHandle !== 0
                                        && root.renamingHandle === gridDelegateItem.handle

        width: GridView.view.cellWidth
        height: GridView.view.cellHeight

        Component.onCompleted: {
            if (gridDelegateItem.hasThumbnail && !gridDelegateItem.isFolder)
                root.thumbController.requestThumbnail(gridDelegateItem.handle);
        }

        Rectangle {
            anchors.fill: parent
            radius: 4
            color: gridDelegateItem.selected ? Qt.rgba(sysPalette.highlight.r,
                                                       sysPalette.highlight.g,
                                                       sysPalette.highlight.b, 0.35) : "transparent"
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 4
            spacing: 2

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Image {
                    anchors.fill: parent
                    visible: gridDelegateItem.hasThumbnail && !gridDelegateItem.isFolder
                             && gridDelegateItem.thumbnailPath !== ""
                    // thumbnailPath uses native (backslash-on-Windows)
                    // separators -- normalize before building a URL.
                    source: gridDelegateItem.thumbnailPath ? ("file:///"
                                                              + gridDelegateItem.thumbnailPath.replace(
                                                                  /\\/g, "/")) : ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                }

                Label {
                    anchors.centerIn: parent
                    visible: !gridDelegateItem.hasThumbnail || gridDelegateItem.isFolder
                             || gridDelegateItem.thumbnailPath === ""
                    text: gridDelegateItem.isFolder ? "📁" : "📄"
                    font.pixelSize: 32
                }
            }

            Label {
                visible: !gridDelegateItem.renaming
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideMiddle
                text: gridDelegateItem.name
            }

            // Takes the name label's slot in the tile. Inline Component for the
            // same required-property reason as FileTableView.qml's.
            Loader {
                Layout.fillWidth: true
                active: gridDelegateItem.renaming
                sourceComponent: Component {
                    InlineRenameField {
                        originalName: gridDelegateItem.name
                        isFolder: gridDelegateItem.isFolder
                        onCommitted: newName => root.commitRename(gridDelegateItem.handle,
                                                                  gridDelegateItem.name, newName)
                        onCancelled: Qt.callLater(root.endRename)
                    }
                }
            }
        }

        // Left-click selection is handled entirely by the view-level
        // background TapHandler above (see its comment) -- this one is
        // double-click-only.
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onDoubleTapped: root.activateRequested(gridDelegateItem.isFolder,
                                                   gridDelegateItem.handle, gridDelegateItem.name,
                                                   gridDelegateItem.sizeBytes)
        }

        // Folder-only, mirrors FileTableView.qml's row delegate -- a file
        // has nothing sensible to open "in a new tab".
        TapHandler {
            acceptedButtons: Qt.MiddleButton
            onTapped: if (gridDelegateItem.isFolder)
                          root.openInNewTabRequested(gridDelegateItem.handle)
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: {
                if (!gridDelegateItem.selected) {
                    root.forceActiveFocus();
                    root.navController.fileListModel.selectRow(gridDelegateItem.index,
                                                               Qt.NoModifier);
                }
                gridContextMenu.popup();
            }
        }
    }
}
