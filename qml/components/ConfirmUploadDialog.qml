import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls

// One instance for the whole app, in Main.qml, for the same reason as
// NameConflictDialog.qml: every drop target reaches this through a single
// uploadController signal.
//
// Asked before that one, so a confirmed upload can still stop at the
// replace/skip question. The file cap comes before both and refuses outright,
// so this never appears for a drop that was already rejected.
Dialog {
    id: root

    required property var uploads

    // The destination rides along on the dialog rather than being remembered in
    // C++, so it stays alive exactly as long as the question does.
    // destinationHandle is `property var` because a quint64 doesn't survive
    // QML's int/real property types.
    property var filePaths: []
    property int fileCount: 0
    property var destinationHandle: 0
    property bool destinationIsRoot: false

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Confirm upload")
    standardButtons: Dialog.Ok | Dialog.Cancel

    onAccepted: root.uploads.uploadConfirmed(root.filePaths, root.destinationHandle,
                                             root.destinationIsRoot)

    Label {
        width: 360
        wrapMode: Text.Wrap
        // The count is recursive, so a dropped folder reads as its contents. No
        // list of names: the point is the size of what was dropped.
        text: qsTr("Upload %n file(s)?", "", root.fileCount)
    }

    Connections {
        target: root.uploads

        function onUploadRequiresConfirmation(filePaths, fileCount, destinationHandle,
                                              destinationIsRoot) {
            root.filePaths = filePaths;
            root.fileCount = fileCount;
            root.destinationHandle = destinationHandle;
            root.destinationIsRoot = destinationIsRoot;
            root.open();
        }
    }
}
