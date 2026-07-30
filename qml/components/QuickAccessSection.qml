import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml/Breadcrumb.qml/FileTableView.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// Pinned-folder list (Phase 11), sitting above the folder tree inside
// SidePanel.qml -- Explorer's own placement. Flat by design: a pin is a
// shortcut to one folder, never expandable, so this is a ListView and not a
// second root inside FolderTreeModel's tree.
//
// model is quickAccessModel, an app-lifetime context property shared across
// every tab (main.cpp). navController is the *active* tab's
// FolderNavigationController, rebound by Main.qml on each tab switch -- same
// arrangement as FolderTreePanel.qml, and the isCurrent formula below is
// deliberately identical to that file's so both halves of the panel highlight
// the current folder the same way.
ColumnLayout {
    id: root

    required property var navController

    // Passed in rather than read off root.height: this ColumnLayout's own
    // height is derived from its children, so capping a child against it would
    // be a binding loop. SidePanel supplies the panel's own (SplitView-driven)
    // height instead.
    required property real availableHeight

    spacing: 0

    // Hidden entirely, header included, while nothing is pinned -- an empty
    // labeled box above the tree would just be dead space.
    visible: quickAccessModel.count > 0

    SystemPalette {
        id: sysPalette
    }

    FolderPinMenu {
        id: pinMenu
    }

    Label {
        Layout.fillWidth: true
        Layout.leftMargin: 8
        Layout.topMargin: 6
        Layout.bottomMargin: 2
        text: qsTr("Quick access")
        font.bold: true
        opacity: 0.7
        elide: Text.ElideRight
    }

    ListView {
        id: pinList

        Layout.fillWidth: true
        // Sized to its content, but never allowed to push the folder tree out
        // of the panel when a lot of folders are pinned.
        Layout.preferredHeight: contentHeight
        Layout.maximumHeight: root.availableHeight * 0.4

        clip: true
        // Only scrollable once it's actually been capped, so a short list
        // doesn't swallow wheel events meant for the tree below.
        interactive: contentHeight > height

        model: quickAccessModel

        delegate: ItemDelegate {
            id: pinDelegate

            required property string name
            required property var handle

            width: pinList.width
            // Matches the height FolderTreePanel.qml's delegate background
            // restates, so both halves of the panel have the same row rhythm.
            implicitHeight: 24
            // Lines the labels up with the tree's first-level rows below.
            leftPadding: 20

            // Matches TabStrip.qml's TabButton and FolderTreePanel.qml's
            // delegate: without this, clicking a row strands keyboard focus
            // here instead of the file view, deadening its arrow-key
            // navigation until the view is re-clicked.
            focusPolicy: Qt.NoFocus

            text: pinDelegate.name

            readonly property bool isCurrent: root.navController ? (!root.navController.atRoot
                                                                    && pinDelegate.handle
                                                                    === root.navController.currentHandle) :
                                                                   false

            background: Rectangle {
                color: pinDelegate.isCurrent ? Qt.rgba(sysPalette.highlight.r,
                                                       sysPalette.highlight.g,
                                                       sysPalette.highlight.b, 0.35) : "transparent"
            }

            // activate() rather than navigateTo() directly: the pin's target
            // may have been deleted on another device since login, so the model
            // verifies it first and answers with either activated() or
            // missing() (both handled in Main.qml, which is what knows about
            // tabs and dialogs).
            onClicked: quickAccessModel.activate(pinDelegate.handle, false)

            // AbstractButton only accepts LeftButton itself, so these two never
            // compete with onClicked above -- the same arrangement
            // FolderTreePanel.qml's TreeViewDelegate already relies on.
            TapHandler {
                acceptedButtons: Qt.MiddleButton
                onTapped: quickAccessModel.activate(pinDelegate.handle, true)
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: pinMenu.popupFor(pinDelegate.handle, false, pinDelegate.name)
            }
        }
    }
}
