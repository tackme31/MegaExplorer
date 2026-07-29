import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// Windows-Explorer-11-style tab strip: one TabButton per row of
// tabsController (a QAbstractListModel, see TabsController.h) plus a
// trailing "+" button. Sits above the address bar/breadcrumb in Main.qml's
// header.
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
