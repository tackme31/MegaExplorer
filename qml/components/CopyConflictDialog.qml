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
// Four-way, so standardButtons can't express it: a hand-built DialogButtonBox
// whose answers call the controller directly rather than going through
// onAccepted/onRejected.
Dialog {
    id: root

    required property var mutController

    // The batch rides on the dialog rather than being remembered in C++, so it
    // stays alive exactly as long as the question does. destinationHandle is
    // `property var` because a quint64 doesn't survive QML's int/real property
    // types.
    property var entries: []
    property var conflictNames: []
    property var destinationHandle: 0
    property bool destinationIsRoot: false

    // Questions that arrived while one was already being asked -- a Ctrl+drop is
    // still delivered while this is up (a modal overlay blocks mouse and keys,
    // not Qt's drag-and-drop path) and open() does nothing on a visible Popup.
    property var pendingRequests: []

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Files with the same name already exist")

    // Every button closes, so this is the one place the next question can start
    // from.
    onClosed: root.showNextRequest()

    Label {
        // The frame is as wide as its button footer, so a fixed width here would
        // wrap the message well short of the edge.
        width: root.availableWidth
        wrapMode: Text.Wrap
        text: qsTr("%1 file(s) with the same name already exist in the destination:").arg(
                  root.conflictNames.length) + "\n" + root.conflictNames.slice(0, 5).join(", ") + (
                  root.conflictNames.length > 5 ? " …" : "")
    }

    footer: DialogButtonBox {
        Button {
            text: qsTr("Replace all")
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: {
                root.mutController.copyReplacingExisting(root.entries, root.destinationHandle,
                                                         root.destinationIsRoot);
                root.close();
            }
        }
        Button {
            text: qsTr("Keep both")
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: {
                root.mutController.copyRenamingExisting(root.entries, root.destinationHandle,
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

    // Reassigned rather than push()ed: an in-place mutation of a `var` array
    // leaves anything reading it unaware that it changed.
    function showNextRequest() {
        if (root.pendingRequests.length === 0)
            return;
        const next = root.pendingRequests[0];
        root.pendingRequests = root.pendingRequests.slice(1);
        root.entries = next.entries;
        root.conflictNames = next.conflictNames;
        root.destinationHandle = next.destinationHandle;
        root.destinationIsRoot = next.destinationIsRoot;
        root.open();
    }

    Connections {
        target: root.mutController

        function onCopyNameConflict(entries, conflictNames, destination, destinationIsRoot) {
            root.pendingRequests = root.pendingRequests.concat([
                                                                   {
                                                                       "entries": entries,
                                                                       "conflictNames":
                                                                       conflictNames,
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
