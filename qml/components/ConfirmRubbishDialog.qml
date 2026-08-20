import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls

// Confirmation for "move the current selection to the Rubbish bin", one
// instance per file view (alongside that view's FileContextMenu) since the
// action is always about that view's own selection. Same shape as
// SignOutDialog/MissingPinDialog; parent is pinned to the window overlay so it
// still centers on the window despite being declared from inside a view.
//
// The message is composed here from the selection count rather than passed in
// pre-formatted -- same "C++ supplies structured fields, QML supplies wording"
// split as NotificationController/ToastStack.qml.
Dialog {
    id: root

    required property var navController
    required property var mutController

    // Sampled by confirm() below, not bound: the selection must not be able to
    // change the wording out from under an already-open dialog. handles is
    // sampled with them for the same reason -- a background refresh (an upload
    // landing, another tab's move) can prune the selection while this dialog is
    // open, and what gets deleted has to be what the prompt named.
    property int itemCount: 0
    property string firstName: ""
    property var handles: []

    // Single entry point for both the context menu and the Delete key.
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
    Component.onCompleted: StandardButtonLabels.pin(footer)
    title: qsTr("Move to Rubbish bin?")

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
        text: root.itemCount === 1 ? qsTr("Move \"%1\" to the Rubbish bin?").arg(root.firstName) : qsTr(
                                         "Move %1 items to the Rubbish bin?").arg(root.itemCount)
    }

    onAccepted: root.mutController.moveHandlesToRubbish(root.handles)
}
