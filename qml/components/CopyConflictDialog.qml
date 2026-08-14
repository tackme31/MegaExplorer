import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls

// The counterpart of NameConflictDialog.qml's upload question for the two
// in-cloud paths: paste/drop-copy and cut-paste/drop-move. Both ask the same
// thing with different verbs, so one dialog answers both signals rather than
// two near-identical files. One per tab, in TabContentPane.qml, because the
// question comes from that tab's FileMutationController -- a single
// window-wide instance would have to re-bind itself every time the active tab
// changed.
//
// standardButtons can't express it: a hand-built DialogButtonBox whose answers
// call the controller directly rather than going through onAccepted/onRejected.
// A copy hides Continue when only folders collide, because the copy path skips
// a colliding folder either way -- a move has no such case, since moveNode
// never looks at a name. Everything the wording and the button set do here
// follows from docs/investigations/SPEC_NAME_CONFLICT_COPY_MOVE.md section 3.
Dialog {
    id: root

    required property var mutController

    // The batch rides on the dialog rather than being remembered in C++, so it
    // stays alive exactly as long as the question does. destinationHandle is
    // `property var` because a quint64 doesn't survive QML's int/real property
    // types.
    property string operation: "copy" // or "move"
    property var entries: []
    property var conflictingFiles: []
    property var conflictingFolders: []
    property var destinationHandle: 0
    property bool destinationIsRoot: false
    // Only a move announces where the nodes came from, so these stay unset for
    // a copy.
    property var sourceHandle: 0
    property bool sourceIsRoot: false

    // Questions that arrived while one was already being asked -- a Ctrl+drop is
    // still delivered while this is up (a modal overlay blocks mouse and keys,
    // not Qt's drag-and-drop path) and open() does nothing on a visible Popup.
    property var pendingRequests: []

    readonly property int unaffectedCount: Math.max(0, root.entries.length
                                                    - root.conflictingFiles.length
                                                    - root.conflictingFolders.length)

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Items with the same name already exist")

    // A Popup takes its content's *implicit* width, and a Text's implicit width
    // is its unwrapped width -- so without a cap the frame grows to the longest
    // sentence below and, on a small window, pushes its own buttons off-screen.
    readonly property real maxWidth: Overlay.overlay.width - 48
    width: Math.min(implicitWidth, maxWidth)

    // Every button closes, so this is the one place the next question can start
    // from.
    onClosed: root.showNextRequest()

    Label {
        // Capped against the overlay rather than root.availableWidth: reading the
        // dialog's own width here closes a loop through its implicitHeight.
        width: Math.min(implicitWidth, root.maxWidth - root.leftPadding - root.rightPadding)
        wrapMode: Text.Wrap
        text: root.buildMessage()
    }

    footer: DialogButtonBox {
        // Natural widths, right-aligned, rather than the style's default of
        // stretching every button across the footer: that default divides by
        // contentModel's count, which a hidden button still occupies, so the
        // folder-only case would leave a button-sized hole.
        alignment: Qt.AlignRight

        Button {
            text: qsTr("Continue")
            // On the copy path this would silently mean Skip when only folders
            // collide: copyNode cannot merge one, so it is dropped either way.
            visible: root.operation === "move" || root.conflictingFiles.length > 0
            // The box lays its buttons out through a ListView over contentModel,
            // which counts hidden ones -- so `visible` alone leaves a
            // button-sized hole. Collapsing the *implicit* width is what takes it
            // out of the row; overriding `width` instead makes the button render
            // as a sliver when it is shown.
            implicitWidth: visible ? Math.max(implicitBackgroundWidth + leftInset + rightInset,
                                              implicitContentWidth + leftPadding + rightPadding) : 0
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: {
                if (root.operation === "move")
                    root.mutController.moveIgnoringExisting(root.entries, root.destinationHandle,
                                                            root.destinationIsRoot,
                                                            root.sourceHandle, root.sourceIsRoot);
                else
                    root.mutController.copyIgnoringExisting(root.entries, root.destinationHandle,
                                                            root.destinationIsRoot);
                root.close();
            }
        }
        Button {
            text: qsTr("Skip")
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: {
                if (root.operation === "move")
                    root.mutController.moveSkippingExisting(root.entries, root.destinationHandle,
                                                            root.destinationIsRoot,
                                                            root.sourceHandle, root.sourceIsRoot);
                else
                    root.mutController.copySkippingExisting(root.entries, root.destinationHandle,
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

    function buildMessage() {
        const files = root.conflictingFiles.length;
        const folders = root.conflictingFolders.length;
        let head;
        if (files > 0 && folders > 0)
            head = qsTr(
                        "%1 file(s) and %2 folder(s) with the same name already exist in the destination:").arg(
                        files).arg(folders);
        else if (files > 0)
            head = qsTr("%1 file(s) with the same name already exist in the destination:").arg(
                        files);
        else
            head = qsTr("%1 folder(s) with the same name already exist in the destination:").arg(
                        folders);

        const names = root.conflictingFiles.concat(root.conflictingFolders);
        const lines = [head + "\n" + names.slice(0, 5).join(", ") + (names.length > 5 ? " …" : "")];

        if (root.operation === "move") {
            lines.push(qsTr(
                           "\"Continue\" leaves both: MEGA allows two items with the same name and never merges folders."));
        } else {
            if (folders > 0)
                lines.push(qsTr(
                               "Folders are skipped with everything inside them: MEGA cannot merge one folder into another."));
            if (files > 0 && folders > 0)
                lines.push(qsTr(
                               "\"Continue\" affects the files only, keeping the existing ones as earlier versions."));
            else if (files > 0)
                lines.push(qsTr("\"Continue\" keeps the existing files as earlier versions."));
        }
        if (root.unaffectedCount > 0)
            lines.push(root.operation === "move" ? qsTr(
                                                       "The other %1 item(s) are moved either way.").arg(
                                                       root.unaffectedCount) : qsTr(
                                                       "The other %1 item(s) are copied either way.").arg(
                                                       root.unaffectedCount));
        return lines.join("\n\n");
    }

    // Reassigned rather than push()ed: an in-place mutation of a `var` array
    // leaves anything reading it unaware that it changed.
    function showNextRequest() {
        if (root.pendingRequests.length === 0)
            return;
        const next = root.pendingRequests[0];
        root.pendingRequests = root.pendingRequests.slice(1);
        root.operation = next.operation;
        root.entries = next.entries;
        root.conflictingFiles = next.conflictingFiles;
        root.conflictingFolders = next.conflictingFolders;
        root.destinationHandle = next.destinationHandle;
        root.destinationIsRoot = next.destinationIsRoot;
        root.sourceHandle = next.sourceHandle;
        root.sourceIsRoot = next.sourceIsRoot;
        root.open();
    }

    function enqueue(request) {
        root.pendingRequests = root.pendingRequests.concat([request]);
        if (!root.visible)
            root.showNextRequest();
    }

    Connections {
        target: root.mutController

        function onCopyNameConflict(entries, conflictingFiles, conflictingFolders, destination,
                                    destinationIsRoot) {
            root.enqueue({
                             "operation": "copy",
                             "entries": entries,
                             "conflictingFiles": conflictingFiles,
                             "conflictingFolders": conflictingFolders,
                             "destinationHandle": destination,
                             "destinationIsRoot": destinationIsRoot,
                             "sourceHandle": 0,
                             "sourceIsRoot": false
                         });
        }

        function onMoveNameConflict(entries, conflictingFiles, conflictingFolders, destination,
                                    destinationIsRoot, source, sourceIsRoot) {
            root.enqueue({
                             "operation": "move",
                             "entries": entries,
                             "conflictingFiles": conflictingFiles,
                             "conflictingFolders": conflictingFolders,
                             "destinationHandle": destination,
                             "destinationIsRoot": destinationIsRoot,
                             "sourceHandle": source,
                             "sourceIsRoot": sourceIsRoot
                         });
        }
    }
}
