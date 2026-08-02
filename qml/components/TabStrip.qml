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
    spacing: 0

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
    readonly property real preferredWidth: tabBar.count * root.maxTabWidth
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
        clip: true
        currentIndex: tabsController.currentIndex

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

                contentItem: RowLayout {
                    spacing: 4

                    Label {
                        Layout.fillWidth: true
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        text: tabButton.text
                    }

                    ToolButton {
                        text: "×"
                        focusPolicy: Qt.NoFocus
                        implicitWidth: 22
                        implicitHeight: 22
                        onClicked: tabsController.closeTab(tabButton.index)
                    }
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
