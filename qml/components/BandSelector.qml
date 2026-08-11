import QtQuick

// Rubber-band (rectangle) selection gesture for a Flickable-based view
// (Phase 21). Owns the pointer handling, the rectangle's geometry and its
// edge auto-scrolling; the host view owns everything that needs to know what
// the view is made of -- which positions count as an item, and which rows a
// content rectangle covers.
//
// Both file views can set Flickable.acceptedButtons: Qt.NoButton (they do,
// so a left-drag starts a move instead of panning), which is what leaves a
// press-drag on empty space unclaimed for this.
Item {
    id: root

    required property Flickable view

    // Hit test supplied by the host: takes a position in view coordinates,
    // returns whether an item is there. A drag that starts on one is Phase
    // 14a's move drag, not a band -- normally the delegate's own DragHandler
    // has taken the exclusive grab long before this handler would activate,
    // so this is the second line of defence rather than the first.
    required property var isOnItem

    // Host-side veto (inline rename in progress, etc).
    property bool suppressed: false

    readonly property bool active: root.tracking

    // Emitted once at the start of the gesture; additive is Ctrl being held,
    // i.e. "add to the selection that already exists" rather than replace it.
    signal bandStarted(bool additive)
    // The rectangle, in the view's content coordinates. Emitted on every
    // pointer move *and* on every auto-scroll step.
    signal bandChanged(rect contentRect)
    signal bandFinished
    signal bandCanceled

    property bool tracking: false

    // Origin in content coordinates and the live pointer in view coordinates:
    // the pair the rectangle is derived from. Keeping the origin in content
    // space is what makes the band stay anchored to the item it started on
    // while auto-scroll moves the view underneath it.
    property point originContent: Qt.point(0, 0)
    property point pointerView: Qt.point(0, 0)

    // Flickable always positions its contentItem at (-contentX, -contentY), so
    // this is the same mapping contentItem.mapFromItem() would do -- written
    // out because a binding on contentX/contentY re-evaluates during
    // auto-scroll, which a function call on a stationary pointer would not.
    readonly property point pointerContent: Qt.point(root.pointerView.x + root.view.contentX,
                                                     root.pointerView.y + root.view.contentY)

    readonly property rect contentRect: Qt.rect(Math.min(root.originContent.x,
                                                         root.pointerContent.x), Math.min(
                                                    root.originContent.y, root.pointerContent.y),
                                                Math.abs(root.pointerContent.x
                                                         - root.originContent.x), Math.abs(
                                                    root.pointerContent.y - root.originContent.y))

    onContentRectChanged: {
        if (root.tracking)
            root.bandChanged(root.contentRect);
    }

    function finish(canceled) {
        if (!root.tracking)
            return;
        root.tracking = false;
        verticalScroller.release();
        horizontalScroller.release();
        if (canceled)
            root.bandCanceled();
        else
            root.bandFinished();
    }

    // parent: root.view rather than the default, so a press below the last row
    // is still seen -- this component's own root is zero-sized. Qt installs the
    // handler on the Flickable's contentItem rather than on the view itself, so
    // its positions arrive in content coordinates and have to be mapped back;
    // see FileViewInput.qml's viewPos() and
    // docs/investigations/VIEW_HIT_TEST_OFFSET_INVESTIGATION.md.
    DragHandler {
        id: bandDrag

        parent: root.view
        target: null
        acceptedButtons: Qt.LeftButton

        onActiveChanged: {
            if (!bandDrag.active) {
                root.finish(false);
                return;
            }

            const press = root.view.mapFromItem(bandDrag.parent, bandDrag.centroid.pressPosition);
            if (root.suppressed || root.isOnItem(press))
                return; // not a band gesture; stay inert for its duration

            root.pointerView = press;
            root.originContent = Qt.point(press.x + root.view.contentX, press.y
                                          + root.view.contentY);
            root.tracking = true;
            root.bandStarted((bandDrag.centroid.modifiers & Qt.ControlModifier) !== 0);
            root.bandChanged(root.contentRect);
        }

        // The same signal FileGridView/FileTableView drive their move drag off.
        onActiveTranslationChanged: {
            if (!root.tracking)
                return;
            root.pointerView = root.view.mapFromItem(bandDrag.parent, bandDrag.centroid.position);
            verticalScroller.track(root.pointerView.y);
            horizontalScroller.track(root.pointerView.x);
        }

        onCanceled: root.finish(true)
    }

    // One per axis rather than one two-axis scroller: a band drag is the only
    // caller needing both at once, and DragAutoScroller already no-ops on an
    // axis with nothing to scroll, so the horizontal one costs nothing in the
    // grid.
    DragAutoScroller {
        id: verticalScroller
        flickable: root.view
    }

    DragAutoScroller {
        id: horizontalScroller
        flickable: root.view
        horizontal: true
    }

    // Enabled only while a band is live, so it cannot shadow Escape anywhere
    // else -- notably InlineRenameField's, which cannot be open at the same
    // time (this component is suppressed during a rename).
    Shortcut {
        sequence: "Escape"
        enabled: root.tracking
        onActivated: root.finish(true)
    }

    Rectangle {
        parent: root.view
        visible: root.tracking
        x: root.contentRect.x - root.view.contentX
        y: root.contentRect.y - root.view.contentY
        width: root.contentRect.width
        height: root.contentRect.height
        color: Theme.color.band
        border.width: Theme.border.thin
        border.color: Theme.color.accent
    }
}
