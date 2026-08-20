import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls

// Confirmation for "destroy everything in the Rubbish bin". Instantiated twice
// -- once per file view, for the bin's background menu, and once in the side
// panel, for the bin's own row -- rather than hoisted to Main.qml: the two
// sites sit in different subtrees and a shared instance would have to be
// reached through an id neither of them owns.
//
// Samples nothing, unlike its two selection-driven siblings: the target is the
// bin itself, so there is no snapshot that a background refresh could outdate.
Dialog {
    id: root

    required property var mutController

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    standardButtons: Dialog.Yes | Dialog.Cancel
    Component.onCompleted: StandardButtonLabels.pin(footer)
    title: qsTr("Empty Rubbish bin?")

    Label {
        // No item count: nothing here has read the bin, and counting it would mean
        // a listing request whose answer is stale by the time the user clicks Yes.
        text: qsTr("Permanently delete everything in the Rubbish bin? This cannot be undone.")
    }

    onAccepted: root.mutController.emptyRubbishBin()
}
