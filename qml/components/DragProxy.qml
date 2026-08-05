import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml/ToastStack.qml.
import QtQuick.Controls.FluentWinUI3

// The single scene-wide carrier for a move drag & drop gesture (Phase 14a).
//
// Why this exists at all instead of putting Drag.active straight on a file
// delegate: a delegate lives inside GridView/TableView's Flickable viewport,
// which clips it. Dragging one towards the side panel would make the drag
// visual vanish at the viewport edge, and the whole point here is to drop onto
// the folder tree / quick-access pins over there. So the delegate stays put and
// this item -- parented to the window's Overlay by Main.qml, above all clipping
// -- is what actually moves and carries the payload.
//
// It also has to *move*: an internal drag (Drag.dragType's default) emits its
// drag events from the attached item's position changes, so moveTo() below is
// simultaneously "update the ghost" and "tell the DropAreas where we are".
Item {
    id: root

    // The dragged nodes as FileListModel::selectedEntries() maps
    // ({handle, name, isFolder}), snapshotted when the gesture started rather
    // than read live off the source view: the drop can land on the folder tree
    // or a quick-access pin, neither of which belongs to any one tab.
    //
    // The names are here for the copy path -- FileOperationService::uniqueCopyName
    // needs them, and re-resolving every handle at drop time would buy nothing.
    property var entries: []

    // Derived rather than passed alongside entries, so the two can't drift.
    readonly property var handles: root.entries.map(e => e.handle)

    // The FolderNavigationController the drag started from. Drop targets call
    // canDropHandlesOn()/moveHandlesTo() through it -- the move is performed by
    // the source tab (it's the one that has to refresh afterwards), not by
    // whatever happens to be under the cursor.
    property var sourceNav: null

    property string label: ""

    readonly property bool active: root.Drag.active

    // Ghost-only mode (Phase 22a): the quick-access reorder wants the same
    // visual but must not start a Qt drag -- it's confined to one ListView, and
    // any DragEnter reaching the five node-move DropAreas would be noise. So
    // active stays false throughout and only the ghost is borrowed.
    property bool ghostOnly: false

    // Ctrl (without Shift) turns the gesture into a copy, Explorer's rule.
    // Sampled from the OS rather than from any event: QML's DragEvent carries
    // no modifiers at all, and an internal drag delivers nothing at all while
    // the pointer is still -- which is exactly when "I've arrived, now make it
    // a copy" happens. Hence KeyboardState plus the Timer below.
    property bool copyMode: false

    function sampleCopyMode() {
        const mods = KeyboardState.modifiers();
        // Shift wins: it is Explorer's explicit "move", and the only way to
        // spell one while Ctrl is still held down from a Ctrl+click.
        root.copyMode = (mods & Qt.ControlModifier) !== 0 && (mods & Qt.ShiftModifier) === 0;
    }

    // Only covers the stationary pointer: begin(), moveTo() and finish() each
    // sample directly, so the interval never decides anything -- it just keeps
    // the badge and the drop-target highlighting honest between moves.
    Timer {
        running: root.active
        interval: 100
        repeat: true
        onTriggered: root.sampleCopyMode()
    }

    // Filters out anything that isn't this app's own node drag. Nothing else
    // produces this key today, but a DropArea declaring it can never be
    // confused by a future external (file/URL) drop.
    Drag.keys: ["application/x-megaexplorer-nodes"]
    // The ghost hangs just below/right of the cursor rather than under it, so
    // it never covers the row being targeted.
    Drag.hotSpot.x: -12
    Drag.hotSpot.y: -12

    width: ghost.width
    height: ghost.height
    visible: root.active || root.ghostOnly
    // Must not intercept anything: it sits under the cursor for the whole
    // gesture, and DropAreas below it still have to see the drag.
    enabled: false

    SystemPalette {
        id: sysPalette
    }

    Rectangle {
        id: ghost
        width: ghostLabel.implicitWidth + 16
        height: ghostLabel.implicitHeight + 10
        radius: 4
        color: Qt.rgba(sysPalette.highlight.r, sysPalette.highlight.g, sysPalette.highlight.b, 0.85)
        border.color: sysPalette.highlight
        opacity: 0.9

        Label {
            id: ghostLabel
            anchors.centerIn: parent
            text: root.label
            color: sysPalette.highlightedText
        }

        // Explorer's "+" affordance. Guarded on active as well as copyMode: the
        // ghost is also borrowed by the quick-access/tab reorder gestures, which
        // never start a Qt drag and are never a copy.
        Rectangle {
            visible: root.copyMode && root.active
            width: 16
            height: 16
            radius: 8
            anchors.right: parent.left
            anchors.bottom: parent.bottom
            anchors.rightMargin: -6
            anchors.bottomMargin: -4
            color: sysPalette.highlightedText
            border.color: sysPalette.highlight

            Label {
                anchors.centerIn: parent
                text: "+"
                color: sysPalette.highlight
            }
        }
    }

    // scenePos comes from the source view's DragHandler centroid, in scene
    // coordinates -- this item's parent is the Overlay, so it needs mapping.
    function positionAt(scenePos) {
        const local = root.parent.mapFromItem(null, scenePos);
        root.x = local.x + 12;
        root.y = local.y + 12;
    }

    function begin(nav, draggedEntries, text, scenePos) {
        root.sourceNav = nav;
        root.entries = draggedEntries;
        root.label = text;
        // Before Drag.active, like sourceNav: the source view's own DragEnter is
        // delivered from inside that assignment, and the Timer above -- bound to
        // the `active` alias, which hasn't re-evaluated yet -- has not started.
        root.sampleCopyMode();
        root.positionAt(scenePos);
        root.Drag.active = true;
    }

    function moveTo(scenePos) {
        if (!root.active && !root.ghostOnly)
            return;
        if (root.active)
            root.sampleCopyMode();
        root.positionAt(scenePos);
    }

    // Ghost-only counterparts of begin()/finish(): no payload, no Drag.active,
    // so nothing downstream of this item can tell a gesture is running.
    function beginGhost(text, scenePos) {
        root.label = text;
        root.copyMode = false; // a reorder is never a copy; keeps the badge off
        root.positionAt(scenePos);
        root.ghostOnly = true;
    }

    function finishGhost() {
        root.ghostOnly = false;
        root.label = "";
    }

    // Which of the two questions a hovered drop target should be asking. Kept
    // here rather than branched in each of the six targets: they all want the
    // same answer, and the mode is this object's business.
    function canDropOn(handle, isRoot) {
        if (!root.sourceNav)
            return false;
        return root.copyMode ? root.sourceNav.canCopyEntriesOn(root.entries, handle, isRoot) :
                               root.sourceNav.canDropHandlesOn(root.handles, handle, isRoot);
    }

    function dropOn(handle, isRoot) {
        if (!root.sourceNav)
            return;
        if (root.copyMode)
            root.sourceNav.copyEntriesTo(root.entries, handle, isRoot);
        else
            root.sourceNav.moveHandlesTo(root.handles, handle, isRoot);
    }

    // Ends the gesture. Drag.drop() delivers the drop event to whichever
    // DropArea currently accepts it (none, if the cursor is over empty chrome),
    // and clears Drag.active as a side effect.
    function finish() {
        if (!root.active)
            return;
        // Re-sampled here, synchronously, so the modifier held at button-release
        // is the one that decides -- Explorer's rule, and it makes the Timer's
        // interval purely cosmetic. Assigning copyMode fires copyModeChanged,
        // which the hovered DropArea's Connections handles before Drag.drop()
        // below delivers onDropped, so it sees a fresh `accepting`.
        root.sampleCopyMode();
        root.Drag.drop();
        root.Drag.active = false;
        root.entries = [];
        root.sourceNav = null;
        root.label = "";
        root.copyMode = false;
    }

    // Escape / a lost grab: end the gesture without dropping anything.
    function cancel() {
        if (!root.active)
            return;
        root.Drag.cancel();
        root.entries = [];
        root.sourceNav = null;
        root.label = "";
        root.copyMode = false;
    }
}
