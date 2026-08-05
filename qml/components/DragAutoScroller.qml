import QtQuick

// Edge auto-scrolling for a Flickable while a drag hovers over it (Phase 14a).
//
// Qt provides nothing for this. The file views need it because they set
// Flickable.acceptedButtons: Qt.NoButton so that a left-drag starts a move
// instead of panning -- which also takes away the drag-to-edge scrolling a long
// listing would otherwise need. The folder tree needs it because its drag
// events are consumed by DropAreas before Flickable ever sees them.
//
// Declared as a plain child of the Flickable it drives (a non-Item child goes
// into flickableData harmlessly, same as the SystemPalette instances already
// sitting in these views).
Timer {
    id: root

    required property Flickable flickable

    // Which axis to scroll. Vertical by default because that is what all four
    // Phase 14a/22a call sites want; the tab strip (Phase 22b) is the one
    // horizontal user. Only the axis changes -- margin, step and the -1/0/1
    // convention are shared.
    property bool horizontal: false

    // How close to the leading/trailing edge the drag has to be, and how far one
    // tick scrolls. 16ms ticks make that pixels-per-frame at 60Hz.
    property real margin: 24
    property real step: 10

    // -1 towards the start (up/left), 1 towards the end (down/right), 0 idle.
    property int direction: 0

    // The extent along the scrolled axis, so the two branches below aren't
    // written out twice.
    readonly property real viewportExtent: root.horizontal ? root.flickable.width :
                                                             root.flickable.height

    readonly property real contentExtent: root.horizontal ? root.flickable.contentWidth :
                                                            root.flickable.contentHeight

    interval: 16
    repeat: true
    running: root.direction !== 0

    onTriggered: {
        const max = Math.max(0, root.contentExtent - root.viewportExtent);
        if (root.horizontal) {
            const nextX = root.flickable.contentX + root.direction * root.step;
            root.flickable.contentX = Math.max(0, Math.min(nextX, max));
        } else {
            const nextY = root.flickable.contentY + root.direction * root.step;
            root.flickable.contentY = Math.max(0, Math.min(nextY, max));
        }
    }

    // pos is the drag position along the scrolled axis, in the flickable's own
    // coordinates. Deliberately not named stop()/start(): Timer already has
    // those.
    function track(pos) {
        if (pos < root.margin)
            root.direction = -1;
        else if (pos > root.viewportExtent - root.margin)
            root.direction = 1;
        else
            root.direction = 0;
    }

    function release() {
        root.direction = 0;
    }
}
