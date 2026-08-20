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

    // Questions that arrived while one was already being asked. A drop is still
    // delivered while this is up -- a modal overlay blocks mouse and keys, not
    // Qt's drag-and-drop path -- and open() does nothing on a visible Popup, so
    // without this the waiting question would be overwritten and its files
    // silently never uploaded.
    property var pendingRequests: []

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Confirm upload")
    standardButtons: Dialog.Ok | Dialog.Cancel
    Component.onCompleted: StandardButtonLabels.pin(footer)

    onAccepted: root.uploads.uploadConfirmed(root.filePaths, root.destinationHandle,
                                             root.destinationIsRoot)

    // Both answers close, so this is the one place the next question can start
    // from. Cancel drops only the question it answered.
    onClosed: root.showNextRequest()

    Label {
        width: 360
        wrapMode: Text.Wrap
        // The count is recursive, so a dropped folder reads as its contents. No
        // list of names: the point is the size of what was dropped.
        text: qsTr("Upload %n file(s)?", "", root.fileCount)
    }

    // Reassigned rather than push()ed: an in-place mutation of a `var` array
    // leaves anything reading it unaware that it changed.
    function showNextRequest() {
        if (root.pendingRequests.length === 0)
            return;
        const next = root.pendingRequests[0];
        root.pendingRequests = root.pendingRequests.slice(1);
        root.filePaths = next.filePaths;
        root.fileCount = next.fileCount;
        root.destinationHandle = next.destinationHandle;
        root.destinationIsRoot = next.destinationIsRoot;
        root.open();
    }

    Connections {
        target: root.uploads

        function onUploadRequiresConfirmation(filePaths, fileCount, destinationHandle,
                                              destinationIsRoot) {
            root.pendingRequests = root.pendingRequests.concat([{
                                                                   "filePaths": filePaths,
                                                                   "fileCount": fileCount,
                                                                   "destinationHandle": destinationHandle,
                                                                   "destinationIsRoot": destinationIsRoot
                                                               }]);
            if (!root.visible)
                root.showNextRequest();
        }
    }
}
