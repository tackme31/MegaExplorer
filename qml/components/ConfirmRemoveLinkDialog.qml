import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls

// Confirmation for "remove the public link of the selected node", one instance
// per file view like ConfirmRubbishDialog. Worth a prompt for the same reason
// as the two delete dialogs: the URL stops working for everyone it was handed
// to, and re-exporting mints a different one.
Dialog {
    id: root

    required property var navController
    required property var mutController

    // Sampled by confirm(), not bound, exactly as in ConfirmRubbishDialog: a
    // background refresh can prune the selection while the dialog is open, and
    // what loses its link has to be what the prompt named.
    property string firstName: ""
    property var handle: 0

    // The action is offered on a single node only (MenuActionResolver's
    // SingleOnly), so only the first entry is ever the target.
    function confirm() {
        const entries = root.navController.fileListModel.selectedEntries();
        if (entries.length === 0)
            return;
        root.firstName = entries[0].name;
        root.handle = entries[0].handle;
        root.open();
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    standardButtons: Dialog.Yes | Dialog.Cancel
    title: qsTr("Remove link?")

    // A Popup takes its content's *implicit* width, and a Text's implicit width
    // is its unwrapped width -- so without a cap a single long file name in the
    // message below drags the frame past the window edge.
    readonly property real maxWidth: Overlay.overlay.width - 48
    width: Math.min(implicitWidth, maxWidth)

    Label {
        // Capped against the overlay rather than root.availableWidth: reading the
        // dialog's own width here closes a loop through its implicitHeight, which
        // Qt reports 49 times a run.
        width: Math.min(implicitWidth, root.maxWidth - root.leftPadding - root.rightPadding)
        wrapMode: Text.Wrap
        text: qsTr("Remove the link to \"%1\"? Anyone holding it will lose access.").arg(
                  root.firstName)
    }

    onAccepted: root.mutController.removeLink(root.handle)
}
