import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml/ConfirmRubbishDialog.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts

// Name prompt for "New folder". One instance per *tab* (TabContentPane.qml),
// not per view like ConfirmRubbishDialog.qml: the result arrives as a signal
// from the tab's FolderNavigationController, which both views share, so two
// instances would both react to it.
//
// Unlike every other dialog here it can't use standardButtons: an already-taken
// name has to leave the dialog open with the name still in the field, and a
// standard Ok button accepts and closes unconditionally. Hence the hand-built
// DialogButtonBox with an ActionRole Ok, the same escape hatch Main.qml's
// nameConflictDialog uses for its three-way choice.
Dialog {
    id: root

    required property var navController

    // True from Ok until the controller answers. The name check is the
    // server's (see IMegaClient::createFolder), so there is a real round trip
    // to sit through, and re-submitting during it would create two folders.
    property bool busy: false

    // Set from folderCreationFailed's structured reason; the wording is
    // composed here.
    property string errorText: ""

    // Single entry point, samples nothing: unlike ConfirmRubbishDialog there
    // is no selection to freeze, only a blank field to start from.
    function prompt() {
        root.errorText = "";
        root.busy = false;
        nameField.clear();
        root.open();
    }

    function submit() {
        if (root.busy || nameField.text.trim() === "")
            return;
        root.errorText = "";
        root.busy = true;
        // Untrimmed on purpose: FileOperationService::isValidName is the one
        // naming rule and it doesn't trim either, so what the user typed is
        // what the folder is called.
        root.navController.createFolder(nameField.text);
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("New folder")

    onOpened: nameField.forceActiveFocus()

    ColumnLayout {
        spacing: Theme.spacing.md

        TextField {
            id: nameField
            Layout.preferredWidth: 320
            Layout.fillWidth: true
            enabled: !root.busy
            placeholderText: qsTr("Folder name")
            onAccepted: root.submit()
        }

        Label {
            Layout.fillWidth: true
            Layout.maximumWidth: 320
            wrapMode: Text.Wrap
            color: Theme.color.danger
            visible: root.errorText !== ""
            text: root.errorText
        }
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("OK")
            enabled: !root.busy && nameField.text.trim() !== ""
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: root.submit()
        }
        Button {
            text: qsTr("Cancel")
            enabled: !root.busy
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: root.close()
        }
    }

    Connections {
        target: root.navController

        function onFolderCreated() {
            root.busy = false;
            root.close();
        }

        function onFolderCreationFailed(reason) {
            root.busy = false;
            // "other" is anything the user can't fix by editing the name --
            // a toast already carries the message, so get out of the way.
            if (reason === "other") {
                root.close();
                return;
            }
            root.errorText = reason === "exists" ? qsTr("A folder with this name already exists") :
                                                   qsTr("That name can't be used");
            nameField.forceActiveFocus();
            nameField.selectAll();
        }
    }
}
