import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls

// One instance for the whole app, in Main.qml: all five drop targets reach
// this through a single uploadController signal, so a second instance would
// answer the same question twice.
Dialog {
    id: root

    required property var uploads

    // The destination rides along on the dialog instead of being remembered in
    // C++, so it stays alive exactly as long as the question does.
    // destinationHandle is `property var` because a quint64 doesn't survive
    // QML's int/real property types.
    property var filePaths: []
    property int folderCount: 0
    property var destinationHandle: 0
    property bool destinationIsRoot: false

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    standardButtons: Dialog.Yes | Dialog.Cancel
    title: qsTr("Folders can't be uploaded")

    Label {
        text: qsTr("%1 folder(s) will be skipped. Upload the remaining %2 file(s)?").arg(
                  root.folderCount).arg(root.filePaths.length)
    }

    // Cancel needs no handler: nothing has been enqueued yet. The two-dialog
    // chain needs no explicit state machine either -- uploadFiles() raises
    // NameConflictDialog by itself if it finds collisions.
    onAccepted: root.uploads.uploadFiles(root.filePaths, root.destinationHandle,
                                         root.destinationIsRoot)

    Connections {
        target: root.uploads
        function onFolderDropRequiresConfirmation(filePaths, folderCount, destinationHandle,
                                                  destinationIsRoot) {
            root.filePaths = filePaths;
            root.folderCount = folderCount;
            root.destinationHandle = destinationHandle;
            root.destinationIsRoot = destinationIsRoot;
            root.open();
        }
    }
}
