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

    // How close to the top/bottom edge the drag has to be, and how far one tick
    // scrolls. 16ms ticks make that pixels-per-frame at 60Hz.
    property real margin: 24
    property real step: 10

    // -1 up, 1 down, 0 idle.
    property int direction: 0

    interval: 16
    repeat: true
    running: root.direction !== 0

    onTriggered: {
        const maxY = Math.max(0, root.flickable.contentHeight - root.flickable.height);
        const next = root.flickable.contentY + root.direction * root.step;
        root.flickable.contentY = Math.max(0, Math.min(next, maxY));
    }

    // y is the drag position in the flickable's own coordinates. Deliberately
    // not named stop()/start(): Timer already has those.
    function track(y) {
        if (y < root.margin)
            root.direction = -1;
        else if (y > root.flickable.height - root.margin)
            root.direction = 1;
        else
            root.direction = 0;
    }

    function release() {
        root.direction = 0;
    }
}
