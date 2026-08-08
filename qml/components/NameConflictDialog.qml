import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls

// One instance for the whole app, in Main.qml: all five drop targets reach
// this through a single uploadController signal, so a second instance would
// answer the same question twice.
//
// Three-way, so standardButtons can't express it -- a hand-built
// DialogButtonBox with two ActionRole buttons and a RejectRole one, the same
// escape hatch NewFolderDialog.qml uses. The answers call the controller
// directly rather than going through onAccepted/onRejected.
Dialog {
    id: root

    required property var uploads

    // The destination rides along on the dialog instead of being remembered in
    // C++, so it stays alive exactly as long as the question does.
    // destinationHandle is `property var` because a quint64 doesn't survive
    // QML's int/real property types.
    property var filePaths: []
    property var conflictNames: []
    property var destinationHandle: 0
    property bool destinationIsRoot: false

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Files with the same name already exist")

    Label {
        width: 360
        wrapMode: Text.Wrap
        text: qsTr("%1 file(s) with the same name already exist in the destination:").arg(
                  root.conflictNames.length) + "\n" + root.conflictNames.slice(0, 5).join(", ") + (
                  root.conflictNames.length > 5 ? " …" : "")
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Replace")
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: {
                root.uploads.uploadReplacingExisting(root.filePaths, root.destinationHandle,
                                                     root.destinationIsRoot);
                root.close();
            }
        }
        Button {
            text: qsTr("Skip")
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: {
                root.uploads.uploadSkippingExisting(root.filePaths, root.destinationHandle,
                                                    root.destinationIsRoot);
                root.close();
            }
        }
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: root.close()
        }
    }

    Connections {
        target: root.uploads
        function onNameConflictRequiresConfirmation(filePaths, conflictNames, destinationHandle,
                                                    destinationIsRoot) {
            root.filePaths = filePaths;
            root.conflictNames = conflictNames;
            root.destinationHandle = destinationHandle;
            root.destinationIsRoot = destinationIsRoot;
            root.open();
        }
    }
}
