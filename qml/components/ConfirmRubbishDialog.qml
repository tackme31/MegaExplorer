import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls

// Confirmation for "move the current selection to the Rubbish bin", one
// instance per file view (alongside that view's FileContextMenu) since the
// action is always about that view's own selection. Same shape as Main.qml's
// signOutConfirmDialog/missingPinDialog; parent is pinned to the window
// overlay so it still centers on the window despite being declared from
// inside a view.
//
// The message is composed here from the selection count rather than passed in
// pre-formatted -- same "C++ supplies structured fields, QML supplies wording"
// split as NotificationController/ToastStack.qml.
Dialog {
    id: root

    required property var navController

    // Sampled by confirm() below, not bound: the selection must not be able to
    // change the wording out from under an already-open dialog.
    property int itemCount: 0
    property string firstName: ""

    // Single entry point for both the context menu and the Delete key.
    function confirm() {
        const entries = root.navController.fileListModel.selectedEntries();
        if (entries.length === 0)
            return;
        root.itemCount = entries.length;
        root.firstName = entries[0].name;
        root.open();
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    standardButtons: Dialog.Yes | Dialog.Cancel
    title: qsTr("Move to Rubbish bin?")

    Label {
        text: root.itemCount === 1 ? qsTr("Move \"%1\" to the Rubbish bin?").arg(root.firstName) : qsTr(
                                         "Move %1 items to the Rubbish bin?").arg(root.itemCount)
    }

    onAccepted: root.navController.moveSelectionToRubbish()
}
