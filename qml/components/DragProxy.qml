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

    // Handles of the dragged nodes, snapshotted when the gesture started rather
    // than read live off the source view: the drop can land on the folder tree
    // or a quick-access pin, neither of which belongs to any one tab.
    property var handles: []

    // The FolderNavigationController the drag started from. Drop targets call
    // canDropHandlesOn()/moveHandlesTo() through it -- the move is performed by
    // the source tab (it's the one that has to refresh afterwards), not by
    // whatever happens to be under the cursor.
    property var sourceNav: null

    property string label: ""

    readonly property bool active: root.Drag.active

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
    visible: root.active
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
    }

    // scenePos comes from the source view's DragHandler centroid, in scene
    // coordinates -- this item's parent is the Overlay, so it needs mapping.
    function positionAt(scenePos) {
        const local = root.parent.mapFromItem(null, scenePos);
        root.x = local.x + 12;
        root.y = local.y + 12;
    }

    function begin(nav, draggedHandles, text, scenePos) {
        root.sourceNav = nav;
        root.handles = draggedHandles;
        root.label = text;
        root.positionAt(scenePos);
        root.Drag.active = true;
    }

    function moveTo(scenePos) {
        if (!root.active)
            return;
        root.positionAt(scenePos);
    }

    // Ends the gesture. Drag.drop() delivers the drop event to whichever
    // DropArea currently accepts it (none, if the cursor is over empty chrome),
    // and clears Drag.active as a side effect.
    function finish() {
        if (!root.active)
            return;
        root.Drag.drop();
        root.Drag.active = false;
        root.handles = [];
        root.sourceNav = null;
        root.label = "";
    }

    // Escape / a lost grab: end the gesture without dropping anything.
    function cancel() {
        if (!root.active)
            return;
        root.Drag.cancel();
        root.handles = [];
        root.sourceNav = null;
        root.label = "";
    }
}
