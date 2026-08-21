import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls

// The full list behind the count link in NameConflictDialog.qml and
// CopyConflictDialog.qml. Those two spell a lone collision out inline; a batch
// used to arrive as a comma-run truncated at five, which neither fits the frame
// nor lets anyone check what is about to be replaced.
//
// Read-only: the question stays on the dialog underneath, so this one only
// closes back to it rather than repeating the answers.
Dialog {
    id: root

    // [{ name: string, isFolder: bool }], in the order the caller listed them.
    property var entries: []

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    standardButtons: Dialog.Close
    Component.onCompleted: StandardButtonLabels.pin(footer)
    title: qsTr("Items with the same name")

    // A Popup takes its content's *implicit* size, so an uncapped list would drag
    // the frame past the window edge on both axes. maxListHeight is what the list
    // asks for -- the window less an allowance for the title, the footer and the
    // paddings, but never less than three rows, so a short window still gets a
    // list rather than an empty frame. That floor is why the height needs its own
    // clamp as well: when it wins, list + chrome is taller than the window, and
    // only this line stops the frame growing past it (the list scrolls instead).
    //
    // Guarded, unlike the sibling dialogs': this is the one declared inside
    // another Dialog, so these first evaluate while the outer popup still has no
    // window and Overlay.overlay is null. The attached property notifies when the
    // overlay arrives, so the fallbacks only have to be harmless, not right.
    readonly property Item overlayItem: Overlay.overlay
    readonly property real maxWidth: root.overlayItem ? root.overlayItem.width - 48 : 0
    readonly property real maxListHeight: Math.max(Theme.rowHeight.compact * 3,
                                                   root.overlayItem
                                                       ? root.overlayItem.height - 160
                                                       : 0)
    width: Math.min(implicitWidth, maxWidth)
    height: root.overlayItem
        ? Math.min(implicitHeight, root.overlayItem.height - 48)
        : implicitHeight

    ListView {
        id: list

        implicitWidth: Math.min(400, root.maxWidth - root.leftPadding - root.rightPadding)
        implicitHeight: Math.min(contentHeight, root.maxListHeight)
        model: root.entries
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ViewScrollBar {}

        delegate: Item {
            id: row

            required property var modelData

            width: ListView.view.width
            height: Theme.rowHeight.compact

            FileIcon {
                id: rowIcon
                anchors.verticalCenter: parent.verticalCenter
                isFolder: row.modelData.isFolder
                fileName: row.modelData.name
            }

            Label {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: rowIcon.right
                anchors.leftMargin: Theme.spacing.md
                anchors.right: parent.right
                // Middle rather than end: the tail of a name that collided is
                // the half that tells two near-identical ones apart.
                elide: Text.ElideMiddle
                text: row.modelData.name
            }
        }
    }
}
