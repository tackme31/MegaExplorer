import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls

// ConfirmRubbishDialog's counterpart for the Rubbish bin screen: the same
// selection-shaped confirmation, but for a delete that destroys. One instance
// per file view, for the same reason as that one.
//
// The wording is emphatic where ConfirmRubbishDialog's is neutral, and that is
// the whole point of it being a separate file rather than a mode flag: binning
// is undone by Restore, this is undone by nothing.
Dialog {
    id: root

    required property var navController
    required property var mutController

    // Sampled by confirm(), not bound, exactly as in ConfirmRubbishDialog: a
    // background refresh can prune the selection while the dialog is open, and
    // what gets destroyed has to be what the prompt named.
    property int itemCount: 0
    property string firstName: ""
    property var handles: []

    function confirm() {
        const entries = root.navController.fileListModel.selectedEntries();
        if (entries.length === 0)
            return;
        root.itemCount = entries.length;
        root.firstName = entries[0].name;
        root.handles = entries.map(e => e.handle);
        root.open();
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    standardButtons: Dialog.Yes | Dialog.Cancel
    title: qsTr("Delete permanently?")

    Label {
        text: root.itemCount === 1 ? qsTr("Delete \"%1\" permanently? This cannot be undone.").arg(
                                         root.firstName) : qsTr(
                                         "Delete %1 items permanently? This cannot be undone.").arg(
                                         root.itemCount)
    }

    onAccepted: root.mutController.deleteHandlesPermanently(root.handles)
}
