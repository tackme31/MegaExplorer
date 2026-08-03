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

    // Per-tab bounds, both enforced by the TabButton width binding below. A
    // tab is never wider than maxTabWidth however few tabs there are
    // (Explorer/Chrome both cap it), and never narrower than minTabWidth
    // however many -- past that the bar scrolls instead of shrinking further.
    readonly property int maxTabWidth: 200
    readonly property int minTabWidth: 80

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

        Repeater {
            model: tabsController

            TabButton {
                id: tabButton
                required property int index
                required property string title
                required property bool atRoot

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
                width: Math.max(root.minTabWidth, Math.min(root.maxTabWidth, tabBar.availableWidth
                                                           / tabBar.count))

                onClicked: tabsController.currentIndex = tabButton.index

                // Middle-click closes the tab, same as a real browser's tab
                // strip -- additive to the button's own left-click handling
                // above, different pointer button so no gesture conflict.
                TapHandler {
                    acceptedButtons: Qt.MiddleButton
                    onTapped: tabsController.closeTab(tabButton.index)
                }

                // The close button is a sibling of contentItem, not a child of
                // it, and that placement is load-bearing: TabButton derives its
                // implicitHeight from implicitContentHeight, so anything taller
                // than the label in there sets the tab's height. Out here the
                // 36px above stands, whatever the button turns out to measure.
                contentItem: RowLayout {
                    spacing: Theme.spacing.md

                    // Explorer puts a folder on every tab; a tab always shows a
                    // folder's contents, so there is nothing to switch on (S4).
                    FileIcon {
                        isFolder: true
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
}
