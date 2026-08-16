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
    // Already spelled out relative to the drop by the controller, so a hit nested
    // inside a dropped folder reads "folder/sub/name" rather than a bare leaf.
    property var conflictNames: []
    // How many files "Skip" would still upload. Without it Skip and Cancel look
    // like the same answer (SPEC_NAME_CONFLICT_UPLOAD 3-0).
    property int unaffectedCount: 0
    // Formatted by the controller -- QML has no formattedDataSize -- and empty when
    // the total is zero, which is what drops the parenthetical below.
    property string conflictingSize: ""
    property string unaffectedSize: ""
    property var destinationHandle: 0
    property bool destinationIsRoot: false

    // Bound to accountController by whoever declares this dialog, rather than read
    // off the root context here, so the QML test can instantiate it without
    // main.cpp's context properties. Defaults to the SDK's own default for an
    // account that never set the attribute.
    property bool fileVersioningEnabled: true

    // Questions that arrived while one was already being asked -- same hazard and
    // same fix as ConfirmUploadDialog.qml: a drop still reaches the app while this
    // is up, and open() does nothing on a visible Popup.
    property var pendingRequests: []

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Files with the same name already exist")

    // A Popup takes its content's *implicit* width, and a Text's implicit width
    // is its unwrapped width -- so without a cap the frame follows the name list
    // below and, on a small window, pushes its own buttons off-screen.
    readonly property real maxWidth: Overlay.overlay.width - 48
    width: Math.min(implicitWidth, maxWidth)

    // Every one of the three buttons closes, so this is the one place the next
    // question can start from.
    onClosed: root.showNextRequest()

    Label {
        // Capped against the overlay rather than root.availableWidth: reading the
        // dialog's own width here closes a loop through its implicitHeight.
        width: Math.min(implicitWidth, root.maxWidth - root.leftPadding - root.rightPadding)
        wrapMode: Text.Wrap
        text: root.buildMessage()
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

    // Whole sentences per case rather than clauses joined at runtime: a translator
    // needs the sentence, and there are only six of them.
    function buildMessage() {
        const names = root.conflictNames;
        const head = root.conflictingSize === "" ? qsTr(
                                                       "%1 file(s) with the same name already exist in the destination:").arg(
                                                       names.length) : qsTr(
                                                       "%1 file(s) with the same name already exist in the destination (%2):").arg(
                                                       names.length).arg(root.conflictingSize);
        const lines = [head + "\n" + names.slice(0, 5).join(", ") + (names.length > 5 ? " …" : "")];
        lines.push(root.fileVersioningEnabled ? qsTr(
                                                    "\"Replace\" keeps the existing files as earlier versions.") :
                                                qsTr("\"Replace\" deletes the existing files outright: file versioning is off for this account, so they cannot be recovered."));
        if (root.unaffectedCount > 0)
            lines.push(root.unaffectedSize === "" ? qsTr(
                                                        "The other %1 file(s) are uploaded either way.").arg(
                                                        root.unaffectedCount) : qsTr(
                                                        "The other %1 file(s) are uploaded either way (%2).").arg(
                                                        root.unaffectedCount).arg(
                                                        root.unaffectedSize));
        return lines.join("\n\n");
    }

    // Reassigned rather than push()ed, as in ConfirmUploadDialog.qml.
    function showNextRequest() {
        if (root.pendingRequests.length === 0)
            return;
        const next = root.pendingRequests[0];
        root.pendingRequests = root.pendingRequests.slice(1);
        root.filePaths = next.filePaths;
        root.conflictNames = next.conflictNames;
        root.unaffectedCount = next.unaffectedCount;
        root.conflictingSize = next.conflictingSize;
        root.unaffectedSize = next.unaffectedSize;
        root.destinationHandle = next.destinationHandle;
        root.destinationIsRoot = next.destinationIsRoot;
        root.open();
    }

    Connections {
        target: root.uploads
        function onNameConflictRequiresConfirmation(filePaths, conflictNames, unaffectedCount,
                                                    conflictingSize, unaffectedSize,
                                                    destinationHandle, destinationIsRoot) {
            root.pendingRequests = root.pendingRequests.concat([
                                                                   {
                                                                       "filePaths": filePaths,
                                                                       "conflictNames":
                                                                       conflictNames,
                                                                       "unaffectedCount":
                                                                       unaffectedCount,
                                                                       "conflictingSize":
                                                                       conflictingSize,
                                                                       "unaffectedSize":
                                                                       unaffectedSize,
                                                                       "destinationHandle":
                                                                       destinationHandle,
                                                                       "destinationIsRoot":
                                                                       destinationIsRoot
                                                                   }
                                                               ]);
            if (!root.visible)
                root.showNextRequest();
        }
    }
}
