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
    // Main.qml's window-wide DragProxy -- see its own comment for why the drag
    // is carried by a separate overlay item instead of a delegate.
    required property var dragProxy

    signal activateRequested(bool isFolder, var handle, string name, var sizeBytes)
    // Middle-click on a folder delegate below -- ignored for files, same
    // restriction as the "Open in new tab" context-menu action
    // (FileActionResolver's FoldersOnly/SingleOnly spec).
    signal openInNewTabRequested(var handle)

    model: root.navController.fileListModel
    clip: true
    cellWidth: 120
    cellHeight: 120
    // Same rationale as FileTableView.qml's TableView: Flickable defaults to
    // panning on left-drag, which since Phase 14a is how a move drag & drop
    // starts instead. NoButton disables drag/flick while leaving wheel
    // scrolling untouched (Flickable.acceptedButtons, since 6.9).
    acceptedButtons: Qt.NoButton
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

    // Drag & drop (Phase 14a). Both halves live at the view level rather than
    // per delegate, matching the TapHandler above: a tile is too small a unit
    // to hit-test against, and the "dropped on empty space" case has no
    // delegate at all.

    // Row a drop would land in, or -1 when it would land on the folder this
    // view is showing (empty space, a file tile, or a folder that refuses the
    // drop). Only meaningful while dropOnCurrentFolder/dropRow are being fed by
    // the DropArea below.
    property int dropRow: -1
    property bool dropOnCurrentFolder: false

    function beginDrag(scenePos) {
        const entries = root.navController.fileListModel.selectedEntries();
        if (entries.length === 0)
            return;
        const label = entries.length === 1 ? entries[0].name : qsTr("%1 items").arg(entries.length);
        root.dragProxy.begin(root.navController, entries.map(e => e.handle), label, scenePos);
    }

    function clearDropTarget() {
        root.dropRow = -1;
        root.dropOnCurrentFolder = false;
    }

    // Reads the payload off root.dragProxy rather than the event's own
    // drag.source. They're the same object -- the DropArea's keys let nothing
    // else in -- but drag.source is typed QObject, so every field access
    // through it is an unchecked dynamic lookup.
    function updateDropTarget(drag) {
        const pos = root.contentItem.mapFromItem(root, Qt.point(drag.x, drag.y));
        const row = root.indexAt(pos.x, pos.y);
        const entry = row < 0 ? ({}) : root.navController.fileListModel.entryAt(row);

        // Internal (move) vs. external (upload) is decided on dragProxy.active,
        // not on drag.hasUrls -- see FolderTreePanel.qml's DropArea for why.
        // Everything downstream (dropRow/dropOnCurrentFolder and the highlight
        // Rectangles they drive) is payload-agnostic; only the question being
        // asked here differs.
        if (root.dragProxy.active) {
            const nav = root.dragProxy.sourceNav;
            const handles = root.dragProxy.handles;

            if (entry.isFolder && nav.canDropHandlesOn(handles, entry.handle, false)) {
                root.dropRow = row;
                root.dropOnCurrentFolder = false;
                return;
            }

            // Anything else in this view means "into the folder being shown",
            // Explorer's own fallback -- which canDropHandlesOn rejects when
            // the dragged items already live there.
            root.dropRow = -1;
            root.dropOnCurrentFolder = nav.canDropHandlesOn(handles,
                                                            root.navController.currentHandle,
                                                            root.navController.atRoot);
            return;
        }

        if (!drag.hasUrls) {
            root.clearDropTarget();
            drag.accepted = false;
            return;
        }

        if (entry.isFolder && uploadController.canUploadTo(entry.handle, false)) {
            root.dropRow = row;
            root.dropOnCurrentFolder = false;
        } else {
            root.dropRow = -1;
            // Unlike the move path, this is true almost always -- an external
            // drag has no "already lives there" case -- so the viewport frame
            // stays lit for most of the gesture. That's Explorer's behavior,
            // not a bug.
            root.dropOnCurrentFolder = uploadController.canUploadTo(root.navController.currentHandle,
                                                                    root.navController.atRoot);
        }
        // Only the external branch touches drag.accepted: the move path relies
        // on implicit acceptance by key match.
        drag.accepted = root.dropRow >= 0 || root.dropOnCurrentFolder;
    }

    DragAutoScroller {
        id: autoScroller
        flickable: root
    }

    DropArea {
        // parent: root for the same reason the TapHandler below uses it -- a
        // plain child of a Flickable lands in contentItem, which scrolls and is
        // only as tall as the content.
        parent: root
        anchors.fill: parent
        // "text/uri-list" is what an external OS drop matches on -- without it
        // those drops are silently ignored here.
        keys: ["application/x-megaexplorer-nodes", "text/uri-list"]

        onEntered: drag => {
            root.updateDropTarget(drag);
            autoScroller.track(drag.y);
        }
        onPositionChanged: drag => {
            root.updateDropTarget(drag);
            autoScroller.track(drag.y);
        }
        onExited: {
            root.clearDropTarget();
            autoScroller.release();
        }
        // Branches on drop.hasUrls, not on dragProxy.active like
        // updateDropTarget above -- see FolderTreePanel.qml's onDropped.
        onDropped: drop => {
            autoScroller.release();
            const target = root.dropRow >= 0 ? root.navController.fileListModel.entryAt(root.dropRow).handle :
                                               root.navController.currentHandle;
            const targetIsRoot = root.dropRow >= 0 ? false : root.navController.atRoot;

            if (root.dropRow >= 0 || root.dropOnCurrentFolder) {
                if (drop.hasUrls) {
                    drop.accept(Qt.CopyAction);
                    uploadController.dropUrls(drop.urls, target, targetIsRoot);
                } else {
                    root.dragProxy.sourceNav.moveHandlesTo(root.dragProxy.handles, target,
                                                           targetIsRoot);
                }
            }
            root.clearDropTarget();
        }
    }

    // Drawn over the whole viewport when a drop would land in the folder this
    // view is showing, since that target has no delegate to highlight.
    Rectangle {
        parent: root
        anchors.fill: parent
        visible: root.dropOnCurrentFolder
        color: "transparent"
        border.width: 2
        border.color: sysPalette.highlight
        radius: 4
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
            if (root.renamingHandle !== 0 && idx === root.navController.fileListModel.cursorRow())
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

        readonly property bool renaming: root.renamingHandle !== 0 && root.renamingHandle
                                         === gridDelegateItem.handle

        readonly property bool dropTarget: root.dropRow === gridDelegateItem.index

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
            // Outlined rather than filled, so a drop target that also happens
            // to be selected still reads as two distinct states.
            border.width: gridDelegateItem.dropTarget ? 2 : 0
            border.color: sysPalette.highlight
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

                FileIcon {
                    anchors.centerIn: parent
                    visible: !gridDelegateItem.hasThumbnail || gridDelegateItem.isFolder
                             || gridDelegateItem.thumbnailPath === ""
                    isFolder: gridDelegateItem.isFolder
                    size: Theme.iconSize.lg
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

        // Starts a move drag. target: null because the tile must stay in the
        // grid -- what moves is Main.qml's DragProxy, which this only steers.
        // Passing the threshold makes this take the exclusive grab, which
        // cancels the view-level TapHandler's pending tap; that is what keeps
        // a drag off an already-selected tile from collapsing the selection.
        DragHandler {
            id: dragHandler
            target: null

            onActiveChanged: {
                if (!dragHandler.active) {
                    root.dragProxy.finish();
                    return;
                }
                // Explorer's rule: dragging an unselected tile selects it
                // first, dragging a selected one carries the whole selection.
                if (!gridDelegateItem.selected) {
                    root.forceActiveFocus();
                    root.navController.fileListModel.selectRow(gridDelegateItem.index,
                                                               Qt.NoModifier);
                }
                root.beginDrag(dragHandler.centroid.scenePosition);
            }

            // activeTranslation is the documented "changes on every move"
            // property; centroid is read for the position it changed to.
            onActiveTranslationChanged: if (dragHandler.active)
                                            root.dragProxy.moveTo(
                                                        dragHandler.centroid.scenePosition)

            onCanceled: root.dragProxy.cancel()
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
