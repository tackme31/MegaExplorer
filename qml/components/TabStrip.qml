import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// Windows-Explorer-11-style tab strip: one TabButton per row of
// tabsController (a QAbstractListModel, see TabsController.h) plus a
// trailing "+" button. Lives inside CaptionBar, on the window's caption row
// itself (Phase 17b) -- before that it sat above the address bar/breadcrumb
// in Main.qml's header.
//
// checkable is deliberately false on every TabButton below, with `checked`
// driven by an explicit binding instead of TabBar's own click-driven
// exclusive-group bookkeeping: TabBar normally reacts to a TabButton's
// `checked` becoming true by writing its own currentIndex, and any such
// external write (QML or C++) permanently tears off a previously-declared
// binding on that property. tabsController.currentIndex is the single
// source of truth (Main.qml's central StackLayout is bound to it too), so
// TabBar.currentIndex below must stay a pure one-way display binding for the
// lifetime of the app -- checkable: false keeps TabBar from ever writing it
// itself, leaving onClicked below as the only path that changes
// tabsController.currentIndex.
RowLayout {
    id: root
    // Sole gap in this row -- tabs themselves stay flush against each other
    // (separated by a hairline, see below), Explorer/Chrome style, and only the
    // "+" sits apart from them.
    spacing: Theme.spacing.sm

    // Passed down from CaptionBar, same explicit-hand-off convention the rest
    // of components/ uses for navController/dragProxy.
    required property WindowAgent windowAgent
    required property var dragProxy

    // Per-tab bounds, both enforced by the TabButton width binding below. A
    // tab is never wider than maxTabWidth however few tabs there are
    // (Explorer/Chrome both cap it), and never narrower than minTabWidth
    // however many -- past that the bar scrolls instead of shrinking further.
    readonly property int maxTabWidth: 240
    readonly property int minTabWidth: 80

    // Hoisted out of the TabButton width binding it used to be written in
    // (the comment there explains why the width is explicit at all): the
    // reorder below converts a pointer position into an insertion index by
    // arithmetic, which needs the tab pitch as a number rather than as a
    // property of whichever delegate happens to be under the cursor. Every
    // tab is this wide, which is what makes that arithmetic legal.
    readonly property real tabWidth: Math.max(root.minTabWidth, Math.min(root.maxTabWidth,
                                                                         tabBar.availableWidth
                                                                         / tabBar.count))

    // What CaptionBar sizes us to when the caption row has the space, so the
    // strip stops growing once every tab is at maxTabWidth and the leftover
    // stays draggable.
    readonly property real preferredWidth: tabBar.count * root.maxTabWidth + root.spacing
                                           + addTabButton.implicitWidth

    // Called by CaptionBar's own registerWithAgent(), which the root
    // Component.onCompleted in Main.qml drives after windowAgent.setup() --
    // see CaptionBar.qml for why registration can't happen from here.
    //
    // Registering the two container items is enough: QWindowKit tests the
    // *rect* of each registered item, so tabBar covers every TabButton and
    // its "×" without the Repeater-created delegates having to register
    // themselves. Unregistering is likewise unnecessary -- these two outlive
    // the window, and the agent stores QPointers regardless.
    function registerWithAgent(): void {
    root.windowAgent.setHitTestVisible(tabBar, true);
    root.windowAgent.setHitTestVisible(addTabButton, true);
}

    TabBar {
        id: tabBar
        Layout.fillWidth: true
        // Without this the bar keeps its own implicitHeight and, since neither
        // this RowLayout nor CaptionBar clips, anything taller than the caption
        // row spills below it. That is how the active tab's indicator ended up
        // drawn on top of the breadcrumb row.
        Layout.fillHeight: true
        clip: true
        currentIndex: tabsController.currentIndex

        // Fluent's is an Impl.StyleImage whose filePath is empty in every
        // state: it paints nothing at all, yet still reports an implicit
        // 470x48 that this row would have to fight. Dropping it costs no
        // pixels and lets CaptionBar's surface show through.
        background: null

        // Fluent's own 4/4, except the bottom is deliberately zero: the tab is
        // the selection indicator now (see TabButton.background), so its lower
        // edge has to reach the caption row's bottom and continue into the
        // toolbar underneath. CaptionBar.implicitHeight is 4 + 36 + 0.
        topPadding: Theme.spacing.sm
        bottomPadding: 0

        // A tab can see its own `hovered` but not its neighbour's, and the
        // hairline below has to disappear on both sides of whichever tab is
        // hovered. Tracking it here is the cheapest shared state; -1 means
        // nothing is hovered.
        property int hoveredIndex: -1

        // Reorder state (Phase 22b) lives here rather than on the dragged
        // delegate, same reason Phase 22a put the pin version on the view: the
        // insertion line below reads it, and it has to outlive whatever
        // happens to the delegate while the strip scrolls under the drag.
        // reorderFrom is a row, reorderInsert an insertion point (0..count).
        property int reorderFrom: -1
        property int reorderInsert: -1

        // Arithmetic, not itemAt(): the strip can auto-scroll mid-drag and
        // TabBar's own ListView only resolves realized delegates. Rounding
        // rather than flooring puts the boundary at each tab's midpoint, which
        // is what makes the line land *between* tabs.
        function insertIndexAt(viewX) {
            const i = Math.round((viewX + tabBar.contentItem.contentX) / root.tabWidth);
            return Math.max(0, Math.min(i, tabBar.count));
        }

        function beginReorder(row) {
            tabBar.reorderFrom = row;
            tabBar.reorderInsert = row;
        }

        function endReorder() {
            tabBar.reorderFrom = -1;
            tabBar.reorderInsert = -1;
            autoScroller.release();
            root.dragProxy.finishGhost();
        }

        function commitReorder() {
            if (tabBar.reorderFrom < 0)
                return;
            const from = tabBar.reorderFrom;
            // Insertion point -> final row: pulling the dragged tab out first
            // shifts everything right of it one slot left.
            const to = tabBar.reorderInsert > from ? tabBar.reorderInsert - 1 :
                                                     tabBar.reorderInsert;


            tabBar.endReorder();
            if (to !== from)
                tabsController.moveTab(from, to);
        }

        Repeater {
            model: tabsController

            TabButton {
                id: tabButton
                required property int index
                required property string title
                required property bool atRoot
                required property bool busy
                // FolderNavigationController* for *this* tab, straight off the
                // model's "navigation" role (TabsController::roleNames) --
                // Main.qml's pane Repeater already reads it the same way. The
                // drop target below needs it to ask where this tab is standing.
                required property var navigation

                checkable: false
                checked: tabButton.index === tabsController.currentIndex
                focusPolicy: Qt.NoFocus
                text: tabButton.atRoot ? qsTr("Cloud Drive") : tabButton.title

                // Fluent's are 12/12/10/10. The top and bottom come down to 8
                // so a 20px label lands the tab on 36 (see CaptionBar's height
                // arithmetic), and the right side is cut to make room for the
                // close button anchored over it -- tabs carrying a close
                // affordance are asymmetric in Explorer too.
                topPadding: Theme.spacing.md
                bottomPadding: Theme.spacing.md
                leftPadding: Theme.spacing.lg
                rightPadding: Theme.spacing.sm

                onHoveredChanged: {
                    if (tabButton.hovered)
                        tabBar.hoveredIndex = tabButton.index;
                    else if (tabBar.hoveredIndex === tabButton.index)
                        tabBar.hoveredIndex = -1;
                }

                // Replacing this is what retires Fluent's own active-tab
                // indicator: that 16x3 accent dash is a Rectangle living inside
                // the style's background, so it leaves with it. It was centred
                // under a left-aligned label and read as belonging to neither.
                // The whole tab is the indicator now.
                background: Rectangle {
                    implicitHeight: 36
                    // Per-corner radii (Qt 6.7+) instead of the usual
                    // square-off-the-bottom overlay hack: the top rounds, the
                    // bottom stays sharp so the tab runs into the toolbar row,
                    // which S3 paints the same surface colour.
                    radius: Theme.radius.md
                    bottomLeftRadius: 0
                    bottomRightRadius: 0
                    color: tabButton.checked ? Theme.color.surface : tabButton.pressed
                                               ? Theme.color.subtlePressed : tabButton.hovered
                                                 ? Theme.color.subtleHover : "transparent"

                    // Same outlined accept feedback the other five drop targets
                    // use, for the same reason: an outline stays legible over
                    // the active tab's opaque fill, a wash would not.
                    border.width: tabDropArea.accepting ? Theme.border.drop : 0
                    border.color: Theme.color.accent

                    // Hairline between two adjacent tabs that are both plain --
                    // without it neighbouring inactive tabs merge into one
                    // block. Suppressed next to the active or hovered tab so it
                    // never cuts across their rounded corner.
                    Rectangle {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.border.thin
                        height: Theme.iconSize.sm
                        color: Theme.color.stroke
                        visible: tabButton.index < tabsController.count - 1 && tabButton.index
                                 !== tabsController.currentIndex && tabButton.index + 1
                                 !== tabsController.currentIndex && tabButton.index
                                 !== tabBar.hoveredIndex && tabButton.index + 1
                                 !== tabBar.hoveredIndex
                    }
                }

                // An *explicit* width, unlike every other control in this
                // file, and deliberately so: TabBar::updateLayout() divides
                // its own width evenly among the tabs whose width is still
                // implicit, ignoring what those implicit widths are. Left
                // implicit, fifteen tabs would each be squeezed to ~60px and
                // elide their labels away to nothing. Assigning width here
                // takes the button out of that pool entirely, so the clamp
                // below is what decides, and the bar scrolls (clip: true
                // above) once the tabs stop fitting.
                width: root.tabWidth

                onClicked: tabsController.currentIndex = tabButton.index

                // Middle-click closes the tab, same as a real browser's tab
                // strip -- additive to the button's own left-click handling
                // above, different pointer button so no gesture conflict.
                TapHandler {
                    acceptedButtons: Qt.MiddleButton
                    onTapped: tabsController.closeTab(tabButton.index)
                }

                // Drag-to-reorder (Phase 22b), built exactly like the
                // quick-access pin reorder: target is null, so the tab itself
                // never leaves its slot -- only the ghost and the insertion
                // line move, and the model is touched once, on release. Taking
                // the exclusive grab is also what cancels this button's pending
                // onClicked, so a reorder never switches tabs on the way past.
                //
                // Note what is *not* here: `yAxis.enabled: false`. It reads
                // like the obvious constraint for a one-row strip, but it gates
                // *activation* as well as translation -- with the unused axis
                // disabled the handler never takes the exclusive grab at all.
                // Phase 22a shipped that bug in its first cut; both axes stay
                // live and the logic below simply ignores y.
                DragHandler {
                    id: reorderHandler
                    target: null
                    acceptedButtons: Qt.LeftButton

                    onActiveChanged: {
                        if (!reorderHandler.active) {
                            tabBar.commitReorder();
                            return;
                        }
                        tabBar.beginReorder(tabButton.index);
                        root.dragProxy.beginGhost(tabButton.text,
                                                  reorderHandler.centroid.scenePosition);
                    }

                    // activeTranslationChanged, not centroidChanged: the
                    // documented "changes on every move" property. The centroid
                    // is only read for the position.
                    onActiveTranslationChanged: {
                        if (tabBar.reorderFrom < 0)
                            return;
                        const scenePos = reorderHandler.centroid.scenePosition;
                        root.dragProxy.moveTo(scenePos);
                        const viewX = tabBar.contentItem.mapFromItem(null, scenePos).x;
                        tabBar.reorderInsert = tabBar.insertIndexAt(viewX);
                        autoScroller.track(viewX);
                    }

                    onCanceled: tabBar.endReorder()
                }

                // Spring-loaded tab + drop target in one (Phase 22b). The
                // three-way branch is the same one all five Phase 14a/14b drop
                // targets use; see QuickAccessSection.qml for why hover keys
                // off dragProxy.active while the drop keys off the event's own
                // payload.
                DropArea {
                    id: tabDropArea
                    anchors.fill: parent
                    keys: ["application/x-megaexplorer-nodes", "text/uri-list"]

                    property bool accepting: false

                    // Re-asks when Ctrl toggles the drag between move and copy;
                    // see FolderTreePanel.qml. Deliberately does not touch
                    // dwellTimer either -- the spring-load clock is "600ms after
                    // entering", and a modifier press is not an entry.
                    Connections {
                        target: root.dragProxy
                        function onCopyModeChanged() {
                            if (tabDropArea.containsDrag && root.dragProxy.active)
                                tabDropArea.accepting = root.dragProxy.canDropOn(
                                            tabButton.navigation.currentHandle,
                                            tabButton.navigation.atRoot);
                        }
                    }

                    onEntered: drag => {
                        if (root.dragProxy.active) {
                            tabDropArea.accepting = root.dragProxy.canDropOn(
                                        tabButton.navigation.currentHandle,
                                        tabButton.navigation.atRoot);
                        } else if (drag.hasUrls) {
                            tabDropArea.accepting = uploadController.canUploadTo(
                                        tabButton.navigation.currentHandle,
                                        tabButton.navigation.atRoot);
                            drag.accepted = tabDropArea.accepting;
                        } else {
                            tabDropArea.accepting = false;
                        }

                        // Armed regardless of `accepting`: this tab's own
                        // current folder may be a bad destination (dragging
                        // within one tab, say) while a subfolder of it is
                        // exactly where the user is heading.
                        if (tabButton.index !== tabsController.currentIndex)
                            dwellTimer.restart();
                    }

                    // Deliberately does *not* touch dwellTimer: an internal
                    // drag only delivers events while the pointer moves (see
                    // DragProxy.qml), so restarting here would either postpone
                    // the switch for as long as the user keeps moving or never
                    // fire at all once they stop. "600ms after entering" is
                    // Explorer's rule too.
                    onPositionChanged: drag => {
                        if (!root.dragProxy.active && drag.hasUrls)
                            drag.accepted = tabDropArea.accepting;
                    }

                    onExited: {
                        tabDropArea.accepting = false;
                        dwellTimer.stop();
                    }

                    onDropped: drop => {
                        dwellTimer.stop();
                        if (tabDropArea.accepting) {
                            if (drop.hasUrls) {
                                drop.accept(Qt.CopyAction);
                                uploadController.dropUrls(drop.urls,
                                                          tabButton.navigation.currentHandle,
                                                          tabButton.navigation.atRoot);
                            } else {
                                root.dragProxy.dropOn(tabButton.navigation.currentHandle,
                                                      tabButton.navigation.atRoot);
                            }
                        }
                        tabDropArea.accepting = false;
                    }
                }

                Timer {
                    id: dwellTimer
                    interval: 300
                    onTriggered: tabsController.currentIndex = tabButton.index
                }

                // The close button is a sibling of contentItem, not a child of
                // it, and that placement is load-bearing: TabButton derives its
                // implicitHeight from implicitContentHeight, so anything taller
                // than the label in there sets the tab's height. Out here the
                // 36px above stands, whatever the button turns out to measure.
                contentItem: RowLayout {
                    spacing: Theme.spacing.md

                    // The fixed box is load-bearing for the same reason the
                    // close button sits outside contentItem (see above):
                    // BusyIndicator's own implicit size is the style's ~32px,
                    // which would set the tab's height. Pinning both children
                    // to the icon size keeps the 36px above standing.
                    Item {
                        Layout.preferredWidth: Theme.iconSize.sm
                        Layout.preferredHeight: Theme.iconSize.sm

                        // Explorer puts a folder on every tab; a tab always
                        // shows a folder's contents, so there is nothing to
                        // switch on (S4). It gives way to the spinner while
                        // this tab has an operation in flight (Phase 20a).
                        FileIcon {
                            anchors.fill: parent
                            isFolder: true
                            visible: !tabButton.busy
                        }

                        BusyIndicator {
                            anchors.fill: parent
                            visible: tabButton.busy
                            // Gated the way LoginView.qml gates its own:
                            // whether the style stops animating a hidden
                            // indicator is style-private, and a stuck
                            // animation drives the render loop for as long as
                            // the tab lives.
                            running: tabButton.busy
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        // Reserves the strip the button overlays; contentItem
                        // is laid out across the full availableWidth
                        // regardless. Unconditional, so the label does not
                        // reflow when hover brings the button in and out (a
                        // hidden Item keeps width). A margin rather than the
                        // padding this used to be: a RowLayout has none.
                        Layout.rightMargin: closeButton.width
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        // Both spelled out rather than inherited: Fluent drove
                        // the label's colour from its own contentItem, which
                        // went away with the background above, and the size
                        // matching Fluent's default is now a stated fact
                        // instead of a coincidence.
                        font.pixelSize: Theme.font.body
                        color: tabButton.checked ? Theme.color.text : Theme.color.textSecondary
                        text: tabButton.text
                    }
                }

                ToolButton {
                    id: closeButton
                    anchors.right: parent.right
                    anchors.rightMargin: tabButton.rightPadding
                    anchors.verticalCenter: parent.verticalCenter
                    // Explorer's rule. The tab is only 80px wide at its
                    // narrowest, so a button parked on every tab would be
                    // permanent clutter; `hovered` covers the whole tab, this
                    // button included, so it cannot flicker itself away.
                    visible: tabButton.checked || tabButton.hovered
                    // Fluent's natural icon-only ToolButton is 38x32 -- padding
                    // 11 either side around a 32x32 background -- which left
                    // 18px of label on an 80px tab. Squeezing it needs the
                    // background replaced as well as the padding: the padding
                    // alone cannot get under the background's implicit 32.
                    // (Forcing implicitWidth *without* touching padding is what
                    // collapsed this glyph to nothing before, cf. B2.)
                    //
                    // All four spelled out rather than the one `padding`:
                    // Fluent binds each side individually, and an individual
                    // binding always beats the grouped shorthand, so `padding`
                    // alone leaves 11 in place and reproduces B2 exactly.
                    topPadding: Theme.spacing.xs
                    bottomPadding: Theme.spacing.xs
                    leftPadding: Theme.spacing.xs
                    rightPadding: Theme.spacing.xs
                    implicitWidth: 20
                    implicitHeight: 20
                    background: Rectangle {
                        radius: Theme.radius.sm
                        color: closeButton.pressed ? Theme.color.subtlePressed :
                                                     closeButton.hovered ? Theme.color.subtleHover :
                                                                           "transparent"
                    }
                    // Same glyph, same font, as the window's own close button a
                    // few pixels to the right (CaptionBar.qml); a literal "×"
                    // beside it was visibly a different shape.
                    font.family: Theme.font.iconFamily
                    font.pixelSize: 10
                    text: ""
                    focusPolicy: Qt.NoFocus
                    onClicked: tabsController.closeTab(tabButton.index)
                }
            }
        }
    }

    ToolButton {
        id: addTabButton
        text: "+"
        focusPolicy: Qt.NoFocus
        onClicked: tabsController.addTab()
    }

    // Fluent's TabBar.contentItem is a ListView, and its click-drag panning
    // would steal the reorder gesture the moment the tabs stop fitting --
    // exactly what QuickAccessSection.qml sets acceptedButtons: Qt.NoButton on
    // its own list for. That contentItem is the style's, so it can only be
    // reached through a Binding. Not `interactive: false`, which would take
    // the wheel with it.
    Binding {
        target: tabBar.contentItem
        property: "acceptedButtons"
        value: Qt.NoButton
    }

    // The strip really does scroll once there are enough tabs (min width 80,
    // and CaptionBar caps how wide the strip may get), so a reorder past the
    // visible range needs this. Writes contentX directly, so the Binding above
    // doesn't get in its way.
    DragAutoScroller {
        id: autoScroller
        flickable: tabBar.contentItem
        horizontal: true
    }

    // Declared here rather than inside tabBar, and reparented: TabBar is a
    // Container, so anything declared in it lands in contentModel and becomes
    // a *tab*. Parented to tabBar rather than to its contentItem for the
    // reason BandSelector.qml documents -- a child of a Flickable rides its
    // contentItem and would scroll away with the tabs.
    Rectangle {
        id: insertLine

        parent: tabBar
        z: 2
        visible: tabBar.reorderFrom >= 0
        width: Theme.border.drop
        color: Theme.color.accent
        y: 0
        height: tabBar.height

        // Clamped into the viewport at both ends, not just computed: the
        // "after the last tab" insertion point lands exactly on the clipped
        // edge, where tabBar's own clip would swallow the line.
        x: Math.max(tabBar.contentItem.x, Math.min(tabBar.contentItem.x + tabBar.reorderInsert
                                                   * root.tabWidth - tabBar.contentItem.contentX,
                                                   tabBar.contentItem.x + tabBar.contentItem.width
                                                   - insertLine.width))
    }
}
