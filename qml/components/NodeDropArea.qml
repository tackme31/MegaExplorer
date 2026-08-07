import QtQuick

// The drop target every per-delegate site shares: a breadcrumb segment, a folder
// tree row, a quick-access pin, a tab. All four ask the same two questions of the
// same two objects and differ only in which node they are asking about, so the
// logic lives here once and the site supplies (targetHandle, targetIsRoot).
//
// The view-level DropAreas in FileGridView/FileTableView deliberately do *not*
// use this: their state is a row index rather than a single verdict, and folding
// them in would mean a component that is two components in a trench coat.
//
// Per-site extras hang off the four drag* signals below rather than off the
// DropArea handlers, which the definition here has already claimed -- a second
// assignment at the use site would replace this one, not run after it.
DropArea {
    id: root

    // The window-wide DragProxy (Main.qml's moveDragProxy). Injected rather than
    // reached by id so this is testable without a scene.
    required property var dragProxy

    // The uploadController context property. Not named uploadController: a
    // property of that name would shadow the context property in this scope, so
    // the site's `uploadController: uploadController` would bind to itself and
    // land as undefined (same trap as Main.qml's moveDragProxy id).
    required property var uploads

    // var, not int: MEGA handles are quint64 and an int would truncate them.
    required property var targetHandle
    required property bool targetIsRoot

    // Recomputed on enter, and again whenever Ctrl toggles the drag between move
    // and copy (the two ask different questions and the answers genuinely differ
    // -- see FolderNavigationController::canCopyEntriesOn). Not on
    // positionChanged: the target can't change without leaving this item first.
    // Sites read this to draw their drop highlight.
    property bool accepting: false

    // Emitted after the verdict is settled and *outside* the branches, so a site
    // hooking them gets called on every drag regardless of whether the drop is
    // allowed -- TabStrip's spring-load has to arm on a refused target too.
    signal dragEntered(var drag)
    signal dragMoved(var drag)
    signal dragExited
    // Emitted before the drop is performed, so accepting still holds its pre-drop
    // value here. TabStrip stops its spring-load clock first thing on drop.
    signal dragDropped(var drop)

    // "text/uri-list" is what an external OS drop matches on: an internal Qt drag
    // is matched against Drag.keys, but a drop coming in from Explorer is matched
    // against its QMimeData's format strings, and without this one those drops
    // are silently ignored.
    keys: ["application/x-megaexplorer-nodes", "text/uri-list"]

    // The four handler bodies are functions so tst_NodeDropArea.qml can call them:
    // a DragEvent cannot be synthesized from QML, so a test that went through the
    // signals would need a real drag session. Call sites use the handlers.

    // Internal vs. external is decided on dragProxy.active, not on drag.hasUrls:
    // hasUrls is a claim about the *event*, while active is a claim about the very
    // object the internal branch then dereferences.
    //
    // Payload read off dragProxy rather than the event's own drag.source: same
    // object (keys let nothing else in), but drag.source is typed QObject and
    // every field access through it would be an unchecked dynamic lookup.
    function evaluateEntry(drag) {
        if (root.dragProxy.active) {
            root.accepting = root.dragProxy.canDropOn(root.targetHandle, root.targetIsRoot);
        } else if (drag.hasUrls) {
            root.accepting = root.uploads.canUploadTo(root.targetHandle, root.targetIsRoot);
            // Only the external branch touches drag.accepted; the move path relies
            // on implicit acceptance via key matching, and assigning here would
            // break it.
            drag.accepted = root.accepting;
        } else {
            root.accepting = false;
        }
        root.dragEntered(drag);
    }

    // External drags need drag.accepted re-asserted on every move; the verdict
    // itself is not recomputed, since the target cannot change without an exit.
    function syncMove(drag) {
        if (!root.dragProxy.active && drag.hasUrls)
            drag.accepted = root.accepting;
        root.dragMoved(drag);
    }

    function clearEntry() {
        root.accepting = false;
        root.dragExited();
    }

    // Branches on drop.hasUrls, not on dragProxy.active like the hover handlers
    // above: DragProxy.finish() calls Drag.drop() to deliver this very event, and
    // Drag.active is cleared as a side effect of that same call, so its value here
    // depends on Qt's internal ordering. The event's own payload doesn't.
    //
    // A target whose folder is gone is rejected by canDropHandlesOn's kENoEnt on
    // the move path, and by canUploadTo's on the upload one.
    function performDrop(drop) {
        root.dragDropped(drop);
        if (root.accepting) {
            if (drop.hasUrls) {
                drop.accept(Qt.CopyAction);
                root.uploads.dropUrls(drop.urls, root.targetHandle, root.targetIsRoot);
            } else {
                root.dragProxy.dropOn(root.targetHandle, root.targetIsRoot);
            }
        }
        root.accepting = false;
    }

    // hovering is a parameter rather than a read of containsDrag so a test can
    // exercise both sides of the guard; the caller below supplies the real one.
    function syncCopyMode(hovering) {
        if (hovering && root.dragProxy.active)
            root.accepting = root.dragProxy.canDropOn(root.targetHandle, root.targetIsRoot);
    }

    // Ctrl can go down while the pointer sits still, and an internal drag delivers
    // no event at all for that. DragProxy's copyMode is the only thing that moves,
    // so this is what re-asks.
    Connections {
        target: root.dragProxy
        function onCopyModeChanged() {
            root.syncCopyMode(root.containsDrag);
        }
    }

    onEntered: drag => root.evaluateEntry(drag)
    onPositionChanged: drag => root.syncMove(drag)
    onExited: root.clearEntry()
    onDropped: drop => root.performDrop(drop)
}
