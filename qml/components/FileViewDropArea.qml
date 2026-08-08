import QtQuick

// The view-level drop target both file views share (Phase 14a, extracted in
// R6-2a). It sits at the view level rather than per delegate for two reasons: a
// tile is too small a unit to hit-test against, and the "dropped on empty space"
// case has no delegate at all.
//
// Deliberately not NodeDropArea: that one resolves a single verdict about one
// known node, while this resolves *which row* the pointer is over, and the two
// asked to share a body would be two components in a trench coat.
//
// No signals are declared because both sites use this identically. Adding one
// later is not free: on a DropArea subclass, `signal entered` / `exited` /
// `dropped` / `positionChanged` fail to load with "Duplicate signal name", so a
// per-site hook has to be prefixed the way NodeDropArea's drag* signals are.
DropArea {
    id: root

    // The viewport this covers, the auto-scroller's target, and this item's own
    // parent. Typed Flickable so FileTableView cannot pass its ColumnLayout root
    // by mistake -- the flickable there is a child.
    required property Flickable view

    // Hit test supplied by the host: a position in view coordinates -> row
    // index, -1 for "nothing there". The one thing the two views genuinely
    // disagree on (GridView resolves it by index plus a gap reject, TableView by
    // cellAtPosition). Named rowAtPos rather than rowAt so the table's
    // `rowAtPos: pos => root.rowAt(pos)` cannot be typo'd into self-recursion.
    required property var rowAtPos

    required property var navController
    required property var mutController

    // The window-wide DragProxy (Main.qml's moveDragProxy). Injected rather than
    // reached by id so this is testable without a scene.
    required property var dragProxy

    // The uploadController context property. Not named uploadController: a
    // property of that name would shadow the context property in this scope, so
    // the site's `uploadController: uploadController` would bind to itself and
    // land as undefined (same trap as Main.qml's moveDragProxy id).
    required property var uploads

    // Corner radius of the viewport outline below. Only FileGridView passes one
    // today; whether the table's outline should match is a look question, parked
    // in docs/REFACTOR_PLANS.md's carry-over section.
    property real outlineRadius: 0

    // Row a drop would land in, or -1 when it would land on the folder this view
    // is showing (empty space, a file tile, or a folder that refuses the drop).
    // The host's delegates read this to draw their own highlight.
    property int dropRow: -1
    property bool dropOnCurrentFolder: false

    // Last position a drag event was delivered at, so the internal branch can be
    // re-run without one -- Ctrl toggling the gesture between move and copy
    // produces no drag event at all while the pointer is still.
    property point lastDragPos: Qt.point(0, 0)

    // "text/uri-list" is what an external OS drop matches on -- without it those
    // drops are silently ignored here.
    keys: ["application/x-megaexplorer-nodes", "text/uri-list"]

    // parent: view because a plain child of a Flickable lands in contentItem,
    // which scrolls and is only as tall as the content.
    parent: root.view
    anchors.fill: parent

    function beginDrag(scenePos) {
        const entries = root.navController.fileListModel.selectedEntries();
        if (entries.length === 0)
            return;
        const label = entries.length === 1 ? entries[0].name : qsTr("%1 items").arg(entries.length);
        root.dragProxy.begin(root.mutController, entries, label, scenePos);
    }

    function clearDropTarget() {
        root.dropRow = -1;
        root.dropOnCurrentFolder = false;
    }

    // The internal (node) branch on its own, resolved from lastDragPos rather
    // than an event, so Ctrl toggling copyMode mid-hover can re-run it.
    //
    // Reads the payload off dragProxy rather than the event's own drag.source.
    // They're the same object -- keys let nothing else in -- but drag.source is
    // typed QObject, so every field access through it is an unchecked dynamic
    // lookup.
    function updateNodeDropTarget() {
        const row = root.rowAtPos(root.lastDragPos);
        const entry = row < 0 ? ({}) : root.navController.fileListModel.entryAt(row);

        if (entry.isFolder && root.dragProxy.canDropOn(entry.handle, false)) {
            root.dropRow = row;
            root.dropOnCurrentFolder = false;
            return;
        }

        // Anything else in this view means "into the folder being shown",
        // Explorer's own fallback -- which a move rejects when the dragged items
        // already live there and a Ctrl+drag copy accepts, since that duplicates
        // them under "... - Copy".
        root.dropRow = -1;
        root.dropOnCurrentFolder = root.dragProxy.canDropOn(root.navController.currentHandle,
                                                            root.navController.atRoot);
    }

    function updateDropTarget(drag) {
        root.lastDragPos = Qt.point(drag.x, drag.y);
        const row = root.rowAtPos(Qt.point(drag.x, drag.y));
        const entry = row < 0 ? ({}) : root.navController.fileListModel.entryAt(row);

        // Internal (move) vs. external (upload). Decided on
        // dragProxy.sourceMutations rather than dragProxy.active, unlike
        // NodeDropArea: this view is where the gesture *starts*, so its own
        // DragEnter is delivered from inside DragProxy.begin()'s
        // `Drag.active = true` assignment -- before the binding behind
        // DragProxy.active has re-evaluated, which leaves it reading false.
        // begin() assigns sourceMutations ahead of that, so it's the one payload
        // signal already true here. Reading active instead sent this event down
        // the external branch, where `drag.accepted = false` rejected the
        // DragEnter outright and Qt then withheld every later position/drop
        // event -- i.e. dropping anywhere in the view the drag came from
        // silently did nothing.
        if (!drag.hasUrls && root.dragProxy.sourceMutations) {
            root.updateNodeDropTarget();
            return;
        }

        if (!drag.hasUrls) {
            root.clearDropTarget();
            drag.accepted = false;
            return;
        }

        if (entry.isFolder && root.uploads.canUploadTo(entry.handle, false)) {
            root.dropRow = row;
            root.dropOnCurrentFolder = false;
        } else {
            root.dropRow = -1;
            // Unlike the move path, this is true almost always -- an external
            // drag has no "already lives there" case -- so the viewport frame
            // stays lit for most of the gesture. That's Explorer's behavior,
            // not a bug.
            root.dropOnCurrentFolder = root.uploads.canUploadTo(root.navController.currentHandle,
                                                                root.navController.atRoot);
        }
        // Only the external branch touches drag.accepted: the move path relies
        // on implicit acceptance by key match.
        drag.accepted = root.dropRow >= 0 || root.dropOnCurrentFolder;
    }

    // Branches on drop.hasUrls, not on dragProxy.sourceMutations like
    // updateDropTarget above -- see NodeDropArea.qml's performDrop.
    //
    // A body rather than an inline handler so tst_FileViewDropArea.qml can call
    // it: a DragEvent cannot be synthesized from QML, so a test going through
    // the handlers would need a real drag session. Same for the three below.
    function performDrop(drop) {
        autoScroller.release();
        const target = root.dropRow >= 0 ? root.navController.fileListModel.entryAt(root.dropRow).handle :
                                           root.navController.currentHandle;
        const targetIsRoot = root.dropRow >= 0 ? false : root.navController.atRoot;

        if (root.dropRow >= 0 || root.dropOnCurrentFolder) {
            if (drop.hasUrls) {
                drop.accept(Qt.CopyAction);
                root.uploads.dropUrls(drop.urls, target, targetIsRoot);
            } else {
                root.dragProxy.dropOn(target, targetIsRoot);
            }
        }
        root.clearDropTarget();
    }

    function trackDrag(drag) {
        root.updateDropTarget(drag);
        autoScroller.track(drag.y);
    }

    function releaseDrag() {
        root.clearDropTarget();
        autoScroller.release();
    }

    // hovering is a parameter rather than a read of containsDrag so a test can
    // exercise both sides of the guard; the caller below supplies the real one.
    function syncCopyMode(hovering) {
        if (hovering && root.dragProxy.sourceMutations)
            root.updateNodeDropTarget();
    }

    DragAutoScroller {
        id: autoScroller
        flickable: root.view
    }

    // Ctrl can go down while the pointer sits still, and an internal drag
    // delivers no event at all for that -- so nothing else here would re-run.
    Connections {
        target: root.dragProxy
        function onCopyModeChanged() {
            root.syncCopyMode(root.containsDrag);
        }
    }

    onEntered: drag => root.trackDrag(drag)
    onPositionChanged: drag => root.trackDrag(drag)
    onExited: root.releaseDrag()
    onDropped: drop => root.performDrop(drop)

    // Drawn over the whole viewport when a drop would land in the folder this
    // view is showing, since that target has no delegate to highlight.
    Rectangle {
        anchors.fill: parent
        visible: root.dropOnCurrentFolder
        color: "transparent"
        border.width: Theme.border.drop
        border.color: Theme.color.accent
        radius: root.outlineRadius
    }
}
