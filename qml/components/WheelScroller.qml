import QtQuick

// Replaces Flickable's own mouse-wheel scrolling on the file views. Qt's moves a
// fixed wheelScrollLines * 24px per notch whatever the row height, and restarts
// its easing from wherever the previous notch had got to, so a fast spin loses
// distance. Here each notch adds to the pending target instead.
//
// Declared as a child of the Flickable it drives. Accepting the event (which a
// blocking WheelHandler does before onWheel runs) keeps it from Flickable's
// wheelEvent; horizontal-only events aren't wanted and still reach it.
WheelHandler {
    id: root

    required property Flickable flickable
    // Pixels per wheel "line"; one notch scrolls Qt.styleHints.wheelScrollLines of these.
    required property real lineHeight

    readonly property real minY: root.flickable.originY - root.flickable.topMargin
    readonly property real maxY: Math.max(root.minY, root.flickable.originY
                                          + root.flickable.contentHeight
                                          + root.flickable.bottomMargin - root.flickable.height)

    property real targetY: 0
    // The last contentY this handler wrote, to notice anything else moving the view.
    property real writtenY: 0

    target: null
    orientation: Qt.Vertical

    function clampY(y) {
        return Math.max(root.minY, Math.min(y, root.maxY));
    }

    onWheel: event => {
        let delta = event.pixelDelta.y;
        if (delta === 0) {
            const lines = Qt.styleHints.wheelScrollLines;
            // WHEEL_PAGESCROLL ("one screen at a time") arrives as a non-positive count.
            const notch = lines > 0 ? lines * root.lineHeight : root.flickable.height;
            delta = event.angleDelta.y / 120 * notch;
        }
        if (delta === 0)
            return;
        const base = root.smoother.running ? root.targetY : root.flickable.contentY;
        root.targetY = root.clampY(base - delta);
        root.writtenY = root.flickable.contentY;
        root.smoother.start();
    }

    property FrameAnimation smoother: FrameAnimation {
        id: smoother

        onTriggered: {
            const view = root.flickable;
            // A key press, a folder change or the drag auto-scroller moved the view.
            if (Math.abs(view.contentY - root.writtenY) > 0.5) {
                smoother.stop();
                return;
            }
            // Re-clamped every frame: the listing can shrink while this is running.
            const to = root.clampY(root.targetY);
            const remaining = to - view.contentY;
            let next;
            if (Math.abs(remaining) < 0.5) {
                next = to;
                smoother.stop();
            } else {
                // Exponential approach, ~95% of the way in 150ms at any frame rate.
                next = view.contentY + remaining * (1 - Math.exp(-smoother.frameTime / 0.05));
            }
            view.contentY = next;
            root.writtenY = view.contentY;
        }
    }
}
