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
// The same four answers in the same order on both paths, whatever collides --
// what changes between the cells is only the wording. Everything the wording and
// the button set do here follows from
// docs/investigations/SPEC_NAME_CONFLICT_COPY_MOVE.md section 3.
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
    // Pairs with conflictingFiles.concat(conflictingFolders): what "Rename" would
    // name each of them. A preview -- the answer re-reads the destination and picks
    // again -- so it is only ever shown as an example, never promised.
    property var renamedTo: []
    // Formatted by the controller -- QML has no formattedDataSize -- and empty both
    // when the total is zero and for a move, which rearranges nodes inside one
    // account and so adds nothing to it (SPEC_NAME_CONFLICT_COPY_MOVE 1-6).
    property string conflictingSize: ""
    property string unaffectedSize: ""
    property var destinationHandle: 0
    property bool destinationIsRoot: false
    // Only a move announces where the nodes came from, so these stay unset for
    // a copy.
    property var sourceHandle: 0
    property bool sourceIsRoot: false

    // Bound to accountController by whoever declares this dialog, rather than read
    // off the root context here, so the QML test can instantiate it without
    // main.cpp's context properties. Defaults to the SDK's own default for an
    // account that never set the attribute.
    property bool fileVersioningEnabled: true

    // The one cell of the four where "Continue" destroys something no one can get
    // back: a copy onto a file's name replaces it outright when versioning is off,
    // and the node does not even reach the rubbish bin
    // (SPEC_NAME_CONFLICT_COPY_MOVE 1-3). Folders never version, so they are not it.
    readonly property bool continueLosesData: root.operation === "copy"
                                              && root.conflictingFiles.length > 0 &&
                                              !root.fileVersioningEnabled

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
    readonly property real textWidthCap: root.maxWidth - root.leftPadding - root.rightPadding
    width: Math.min(implicitWidth, maxWidth)

    // Every button closes, so this is the one place the next question can start
    // from.
    onClosed: root.showNextRequest()

    onOpened: root.markDefaultAnswer()

    // fileVersioningEnabled arrives from an SDK round-trip issued at login, so it can
    // land while this dialog is already up: the message rewords itself to "cannot be
    // recovered" through its binding, and without this the accent and the focus would
    // stay on Continue, pointing Enter at the one answer that destroys data.
    onContinueLosesDataChanged: if (root.visible)
                                    root.markDefaultAnswer()

    // Files first, then folders -- the order the head sentence counts them in,
    // and the order the list dialog shows them in.
    readonly property var conflictNames: root.conflictingFiles.concat(root.conflictingFolders)

    Column {
        spacing: Theme.spacing.sm

        Label {
            // Capped against the overlay rather than root.availableWidth: reading
            // the dialog's own width here closes a loop through its implicitHeight.
            width: Math.min(implicitWidth, root.textWidthCap)
            wrapMode: Text.Wrap
            text: root.headLine()
        }

        // A lone name is spelled out; a batch is a link, because the comma-run
        // this replaced was truncated at five and so could neither be read nor
        // checked against what was about to be replaced.
        Label {
            visible: root.conflictNames.length === 1
            width: Math.min(implicitWidth, root.textWidthCap)
            wrapMode: Text.Wrap
            text: root.namesText()
        }

        Label {
            visible: root.conflictNames.length > 1
            text: root.namesText()
            color: Theme.color.accent
            font.underline: namesLinkHover.hovered

            HoverHandler {
                id: namesLinkHover
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                onTapped: nameListDialog.open()
            }
        }

        Label {
            width: Math.min(implicitWidth, root.textWidthCap)
            wrapMode: Text.Wrap
            visible: text !== ""
            // The paragraph break the sentences used to carry themselves.
            topPadding: Theme.spacing.md
            text: root.detailText()
        }
    }

    ConflictNameListDialog {
        id: nameListDialog
        entries: root.listEntries()

        // Opening a second popup takes the focus off the answer markDefaultAnswer()
        // put it on, and nothing puts it back -- without this, Enter after a look at
        // the list answers nothing, or the wrong thing once focus wanders.
        onClosed: root.markDefaultAnswer()
    }

    footer: DialogButtonBox {
        // Natural widths, right-aligned, rather than the style's default of
        // stretching all four across the footer, which pushes the frame past the
        // message it belongs to.
        alignment: Qt.AlignRight

        Button {
            id: continueButton
            text: qsTr("Continue")
            // highlighted is set from onOpened above, not here.
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

            ToolTip {
                visible: continueButton.hovered
                delay: 500
                text: root.continueLine()
                margins: 6
                // The style derives both the width and the centring x from
                // implicitWidth, which for a Text is its *unwrapped* width -- a
                // whole sentence would leave the window without this cap.
                implicitWidth: Math.min(implicitContentWidth + leftPadding + rightPadding, 360)
            }
        }
        Button {
            id: renameButton
            text: qsTr("Rename")
            DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
            onClicked: {
                if (root.operation === "move")
                    root.mutController.moveRenamingExisting(root.entries, root.destinationHandle,
                                                            root.destinationIsRoot,
                                                            root.sourceHandle, root.sourceIsRoot);
                else
                    root.mutController.copyRenamingExisting(root.entries, root.destinationHandle,
                                                            root.destinationIsRoot);
                root.close();
            }

            ToolTip {
                visible: renameButton.hovered
                delay: 500
                text: root.renameLine()
                margins: 6
                implicitWidth: Math.min(implicitContentWidth + leftPadding + rightPadding, 360)
            }
        }
        Button {
            id: skipButton
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

            ToolTip {
                // Files-only batches leave skipLine() empty, and an empty tooltip
                // is a bare frame, not nothing.
                visible: skipButton.hovered && text !== ""
                delay: 500
                text: root.skipLine()
                margins: 6
                implicitWidth: Math.min(implicitContentWidth + leftPadding + rightPadding, 360)
            }
        }
        Button {
            text: qsTr("Cancel")
            DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
            onClicked: root.close()
        }
    }

    // Both halves of "this is the default": the accent fill, and the focus that makes
    // Enter answer it -- none of the four buttons carries AcceptRole, so Dialog's own
    // Enter handling never fires.
    //
    // Assigned imperatively rather than bound on the buttons because DialogButtonBox
    // writes highlighted on every child it adopts, which kills any binding declared
    // there -- even `highlighted: true` reads back false. A write after the box has
    // taken them sticks.
    function markDefaultAnswer() {
        continueButton.highlighted = !root.continueLosesData;
        renameButton.highlighted = root.continueLosesData;
        (root.continueLosesData ? renameButton : continueButton).forceActiveFocus();
    }

    function headLine() {
        const files = root.conflictingFiles.length;
        const folders = root.conflictingFolders.length;
        const size = root.conflictingSize;
        if (files > 0 && folders > 0)
            return size === "" ? qsTr(
                                     "%1 file(s) and %2 folder(s) with the same name already exist in the destination:").arg(
                                     files).arg(folders) : qsTr(
                                     "%1 file(s) and %2 folder(s) with the same name already exist in the destination (%3):").arg(
                                     files).arg(folders).arg(size);
        if (files > 0)
            return size === "" ? qsTr(
                                     "%1 file(s) with the same name already exist in the destination:").arg(
                                     files) : qsTr(
                                     "%1 file(s) with the same name already exist in the destination (%2):").arg(
                                     files).arg(size);
        return size === "" ? qsTr(
                                 "%1 folder(s) with the same name already exist in the destination:").arg(
                                 folders) : qsTr(
                                 "%1 folder(s) with the same name already exist in the destination (%2):").arg(
                                 folders).arg(size);
    }

    // The name itself when there is one, otherwise the label of the link that
    // opens the full list -- counted the same three ways as the head sentence,
    // so the two never disagree about what collided.
    function namesText() {
        const files = root.conflictingFiles.length;
        const folders = root.conflictingFolders.length;
        if (files + folders === 1)
            return root.conflictNames[0];
        if (files > 0 && folders > 0)
            return qsTr("%1 item(s)").arg(files + folders);
        if (files > 0)
            return qsTr("%1 file(s)").arg(files);
        return qsTr("%1 folder(s)").arg(folders);
    }

    // Same order as conflictNames, but carrying which of the two lists each name
    // came from -- the icon beside it is the only thing that says so.
    function listEntries() {
        return root.conflictingFiles.map(n => ({
                                                   "name": n,
                                                   "isFolder": false
                                               })).concat(root.conflictingFolders.map(n => ({
                                                                                                "name": n,
                                                                                                "isFolder": true
                                                                                            })));
    }

    function detailText() {
        const lines = [];
        // What each answer does lives in that button's tooltip, except the one
        // wording that announces unrecoverable loss: a warning nobody sees unless
        // they hover is not a warning.
        if (root.continueLosesData)
            lines.push(root.continueLine());
        if (root.unaffectedCount > 0)
            lines.push(root.unaffectedLine());
        return lines.join("\n\n");
    }

    // Whole sentences per case rather than clauses joined at runtime: a translator
    // needs the sentence, and there are only five of them.
    function continueLine() {
        const files = root.conflictingFiles.length;
        const folders = root.conflictingFolders.length;
        if (root.operation === "move")
            return qsTr(
                        "\"Continue\" leaves both: MEGA allows two items with the same name and never merges folders.");
        if (files > 0 && folders > 0)
            return root.fileVersioningEnabled ? qsTr(
                                                    "\"Continue\" keeps the existing files as earlier versions, and leaves two folders with the same name -- MEGA never merges one into another.") :
                                                qsTr("\"Continue\" deletes the existing files outright -- file versioning is off for this account, so they cannot be recovered -- and leaves two folders with the same name, since MEGA never merges one into another.");
        if (files > 0)
            return root.fileVersioningEnabled ? qsTr(
                                                    "\"Continue\" keeps the existing files as earlier versions.") :
                                                qsTr("\"Continue\" deletes the existing files outright: file versioning is off for this account, so they do not go to the rubbish bin and cannot be recovered.");
        return qsTr(
                    "\"Continue\" leaves two folders with the same name: MEGA never merges one into another.");
    }

    // A folder is skipped whole -- there is no per-child answer here -- which is
    // not obvious from a list that names only the folder. Empty when nothing but
    // files collide, since Skip needs no explaining there.
    function skipLine() {
        if (root.conflictingFolders.length === 0)
            return "";
        return qsTr("\"Skip\" leaves those folders out entirely, with everything inside them.");
    }

    // The size rides along only on a copy: a move adds nothing to the account, so
    // quoting bytes there would suggest data is being sent that is not.
    function unaffectedLine() {
        if (root.operation === "move")
            return qsTr("The other %1 item(s) are moved either way.").arg(root.unaffectedCount);
        if (root.unaffectedSize === "")
            return qsTr("The other %1 item(s) are copied either way.").arg(root.unaffectedCount);
        return qsTr("The other %1 item(s) are copied either way (%2).").arg(
                    root.unaffectedCount).arg(root.unaffectedSize);
    }

    // The name Rename would give is named outright rather than described, since
    // "renamed automatically" leaves the user unable to tell what to look for
    // afterwards (SPEC_NAME_CONFLICT_COPY_MOVE 3-4). One example is enough for a
    // batch -- the rest follow the same suffix.
    function renameLine() {
        const names = root.conflictNames;
        if (root.renamedTo.length === 0)
            return qsTr("\"Rename\" adds them under names the destination does not use yet.");
        if (names.length === 1)
            return qsTr("\"Rename\" adds \"%1\" as \"%2\" instead.").arg(names[0]).arg(
                        root.renamedTo[0]);
        return qsTr("\"Rename\" adds them under unused names, such as \"%1\" for \"%2\".").arg(
                    root.renamedTo[0]).arg(names[0]);
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
        root.renamedTo = next.renamedTo;
        root.conflictingSize = next.conflictingSize;
        root.unaffectedSize = next.unaffectedSize;
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

        function onCopyNameConflict(entries, conflictingFiles, conflictingFolders, renamedTo,
                                    conflictingSize, unaffectedSize, destination,
                                    destinationIsRoot) {
            root.enqueue({
                             "operation": "copy",
                             "entries": entries,
                             "conflictingFiles": conflictingFiles,
                             "conflictingFolders": conflictingFolders,
                             "renamedTo": renamedTo,
                             "conflictingSize": conflictingSize,
                             "unaffectedSize": unaffectedSize,
                             "destinationHandle": destination,
                             "destinationIsRoot": destinationIsRoot,
                             "sourceHandle": 0,
                             "sourceIsRoot": false
                         });
        }

        function onMoveNameConflict(entries, conflictingFiles, conflictingFolders, renamedTo,
                                    destination, destinationIsRoot, source, sourceIsRoot) {
            root.enqueue({
                             "operation": "move",
                             "entries": entries,
                             "conflictingFiles": conflictingFiles,
                             "conflictingFolders": conflictingFolders,
                             "renamedTo": renamedTo,
                             "conflictingSize": "",
                             "unaffectedSize": "",
                             "destinationHandle": destination,
                             "destinationIsRoot": destinationIsRoot,
                             "sourceHandle": source,
                             "sourceIsRoot": sourceIsRoot
                         });
        }
    }
}
