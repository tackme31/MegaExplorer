import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls

// The paste/drop-copy counterpart of NameConflictDialog.qml's upload question.
// One per tab, in TabContentPane.qml, because the question comes from that
// tab's FileMutationController -- a single window-wide instance would have to
// re-bind itself every time the active tab changed.
//
// standardButtons can't express it: a hand-built DialogButtonBox whose answers
// call the controller directly rather than going through onAccepted/onRejected.
// Replace appears only when a file is among the conflicts -- everything the
// wording and the button set do here follows from
// docs/investigations/SPEC_NAME_CONFLICT_RESOLUTION.md section 3.
Dialog {
    id: root

    required property var mutController

    // The batch rides on the dialog rather than being remembered in C++, so it
    // stays alive exactly as long as the question does. destinationHandle is
    // `property var` because a quint64 doesn't survive QML's int/real property
    // types.
    property var entries: []
    property var conflictingFiles: []
    property var conflictingFolders: []
    property var destinationHandle: 0
    property bool destinationIsRoot: false

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
    width: Math.min(implicitWidth, Overlay.overlay.width - 48)

    // Every button closes, so this is the one place the next question can start
    // from.
    onClosed: root.showNextRequest()

    Label {
        width: root.availableWidth
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
            text: qsTr("Replace")
            // Nothing to replace when only folders collide: MEGA cannot merge
            // one, so the button would silently mean Skip.
            visible: root.conflictingFiles.length > 0
            // The box lays its buttons out through a ListView over contentModel,
            // which counts hidden ones -- so `visible` alone leaves a
            // button-sized hole. Collapsing the *implicit* width is what takes it
            // out of the row; overriding `width` instead makes the button render
            // as a sliver when it is shown.
            implicitWidth: visible ? Math.max(implicitBackgroundWidth + leftInset + rightInset,
                                              implicitContentWidth + leftPadding + rightPadding) : 0
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: {
                root.mutController.copyReplacingExisting(root.entries, root.destinationHandle,
                                                         root.destinationIsRoot);
                root.close();
            }
        }
        Button {
            text: qsTr("Skip")
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: {
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

        if (folders > 0)
            lines.push(qsTr(
                           "Folders are skipped with everything inside them: MEGA cannot merge one folder into another."));
        if (files > 0 && folders > 0)
            lines.push(qsTr("\"Replace\" overwrites the files only."));
        if (root.unaffectedCount > 0)
            lines.push(qsTr("The other %1 item(s) are copied either way.").arg(
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
        root.entries = next.entries;
        root.conflictingFiles = next.conflictingFiles;
        root.conflictingFolders = next.conflictingFolders;
        root.destinationHandle = next.destinationHandle;
        root.destinationIsRoot = next.destinationIsRoot;
        root.open();
    }

    Connections {
        target: root.mutController

        function onCopyNameConflict(entries, conflictingFiles, conflictingFolders, destination,
                                    destinationIsRoot) {
            root.pendingRequests = root.pendingRequests.concat([
                                                                   {
                                                                       "entries": entries,
                                                                       "conflictingFiles":
                                                                       conflictingFiles,
                                                                       "conflictingFolders":
                                                                       conflictingFolders,
                                                                       "destinationHandle":
                                                                       destination,
                                                                       "destinationIsRoot":
                                                                       destinationIsRoot
                                                                   }
                                                               ]);
            if (!root.visible)
                root.showNextRequest();
        }
    }
}
