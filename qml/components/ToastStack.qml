import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// The window's single notification surface (S10), replacing the three separate
// Popups -- DownloadSnackbar, OperationSnackbar, ErrorToast -- that all parked
// themselves at `y = parent.height - height - 16` and so overlapped whenever
// two of them fired at once. Their message templates moved here verbatim as the
// three show*() functions below; the callers in Main.qml still receive the same
// controller signals and pass the same arguments.
//
// Not a Popup. A Popup buys modality, focus and Escape-to-close, none of which
// a toast wants, while its contentItem sits outside any Layout -- the trap that
// produced B8's ignored Layout.maximumWidth and then the binding loop in the
// contentWidth that replaced it. A plain Item sidesteps both. Escape no longer
// dismisses; each card carries a close button instead.
//
// Expects to be a child of ApplicationWindow's contentItem (see Main.qml): the
// geometry below pins the stack to the bottom of whatever it is parented to,
// and the window Overlay -- where the old Popups lived -- extends under the
// status bar.
Item {
    id: root

    // Bottom right, the corner Windows itself notifies in -- and out of the way
    // of the file view's content, which a bottom-centred stack sat on top of.
    // Bound rather than anchored, the way DragProxy does it. Because y is a
    // function of height, the stack keeps its bottom edge pinned as cards
    // animate in and out.
    x: parent ? parent.width - width - Theme.toast.margin : 0
    y: parent ? parent.height - height - Theme.toast.margin : 0
    width: column.width
    height: column.height

    // Every card is this wide. Sizing each one to its own text would either
    // left-align cards of different widths in the column or feed the card's
    // width back into the label that determines it -- the shape of the loop
    // B8's fix left behind.
    readonly property int cardWidth: Math.min(Theme.toast.maxWidth, (parent ? parent.width :
                                                                              Theme.toast.maxWidth)
                                              - Theme.toast.margin * 2)

    property int nextSeq: 0

    ListModel {
        id: toastModel
    }

    // Cards are identified by seq, never by index: a card removes itself from
    // the end of its own fade-out, by which time an unrelated dismissal may
    // have shifted every index below it.
    function dismiss(seq) {
        for (let i = 0; i < toastModel.count; ++i) {
            if (toastModel.get(i).seq === seq) {
                toastModel.remove(i);
                return;
            }
        }
    }

    function push(text, actionText, actionPath) {
        toastModel.append({
                              "seq": root.nextSeq++,
                              "text": text,
                              "actionText": actionText,
                              "actionPath": actionPath
                          });
        // Drop the oldest rather than queue it: a queue delays the newest
        // outcome, which is the one worth reading.
        while (toastModel.count > Theme.toast.maxVisible)
            toastModel.remove(0);
    }

    // Success gets an "Open" button wired to DownloadController::openFile();
    // failure shows only the message, mirroring the "no auto-open" rule this
    // feature is built around. alreadyPresent means the SDK skipped the
    // transfer because an identical file was already there -- without saying so
    // the generic "completed" message is indistinguishable from an overwrite.
    function showDownload(success, fileName, localPath, errorMessage, alreadyPresent) {
        let text;
        if (!success)
            text = qsTr("Failed to download %1: %2").arg(fileName).arg(errorMessage);
        else if (alreadyPresent)
            text = qsTr("%1 is already downloaded").arg(fileName);
        else
            text = qsTr("%1 downloaded").arg(fileName);
        root.push(text, success ? qsTr("Open") : "", localPath);
    }

    // Bulk-operation outcomes, fed by
    // notificationController.operationFinished(context, succeeded, failed).
    // No Undo button: a Rubbish-bin undo is finally expressible since Phase
    // 14a added a general move, but it would need the pre-move parent of every
    // item in the batch, which nothing records.
    function showOperation(context, succeeded, failed) {
        let text = "";
        switch (context) {
        case "move":
            if (failed === 0)
                text = qsTr("Moved %1 item(s)").arg(succeeded);
            else if (succeeded === 0)
                text = qsTr("Failed to move %1 item(s)").arg(failed);
            else
                text = qsTr("Moved %1 item(s), %2 failed").arg(succeeded).arg(failed);
            break;
            // A cut-paste reports under "move" above, not here: it *is* a move.
        case "copy":
            if (failed === 0)
                text = qsTr("Copied %1 item(s)").arg(succeeded);
            else if (succeeded === 0)
                text = qsTr("Failed to copy %1 item(s)").arg(failed);
            else
                text = qsTr("Copied %1 item(s), %2 failed").arg(succeeded).arg(failed);
            break;
        case "moveToRubbish":
            if (failed === 0)
                text = qsTr("Moved %1 item(s) to the Rubbish bin").arg(succeeded);
            else if (succeeded === 0)
                text = qsTr("Failed to move %1 item(s) to the Rubbish bin").arg(failed);
            else
                text = qsTr("Moved %1 item(s) to the Rubbish bin, %2 failed").arg(succeeded).arg(
                            failed);
            break;
        case "upload":
            if (failed === 0)
                text = qsTr("Uploaded %1 file(s)").arg(succeeded);
            else if (succeeded === 0)
                text = qsTr("Failed to upload %1 file(s)").arg(failed);
            else
                text = qsTr("Uploaded %1 file(s), %2 failed").arg(succeeded).arg(failed);
            break;
            // Whole batch failed because the destination was gone by the time its
            // turn came -- the count adds nothing, the reason is the point.
        case "uploadDestinationGone":
            text = qsTr("The upload destination folder no longer exists");
            break;
            // Always exactly one folder, so no count and no partial-failure
            // wording -- and the failing cases the user can fix (a duplicate
            // name, an invalid one) never reach a toast at all, they stay in
            // NewFolderDialog.
        case "createFolder":
            text = qsTr("Folder created");
            break;
        }
        if (text !== "")
            root.push(text, "", "");
    }

    // Errors from controllers with no dedicated feedback path, fed by
    // notificationController.errorOccurred(context, errorMessage). context
    // selects the sentence; the raw message is the fallback.
    function showError(context, errorMessage) {
        let text;
        switch (context) {
        case "navigation":
            text = qsTr("Failed to load folder: %1").arg(errorMessage);
            break;
        case "search":
            text = qsTr("Search failed: %1").arg(errorMessage);
            break;
        case "thumbnail":
            text = qsTr("Failed to load thumbnail: %1").arg(errorMessage);
            break;
        case "openFile":
            text = qsTr("Failed to open file: %1").arg(errorMessage);
            break;
        case "rename":
            text = qsTr("Failed to rename: %1").arg(errorMessage);
            break;
        case "createFolder":
            text = qsTr("Failed to create folder: %1").arg(errorMessage);
            break;
            // The whole paste was refused before anything was attempted (a
            // read-only share, a destination that vanished). Per-item failures
            // never come here -- they land in the "copy"/"move" tally above.
        case "paste":
            text = qsTr("Can't paste here: %1").arg(errorMessage);
            break;
            // Ctrl+drop's counterpart of "paste" above, and the same rule: only
            // whole-drop refusals land here (a read-only destination, a folder
            // that vanished, a destination listing that couldn't be read). The
            // per-item tally is the "copy" case in showOperation.
        case "copy":
            text = qsTr("Can't copy here: %1").arg(errorMessage);
            break;
            // Deliberately drops errorMessage: the cause is a local settings
            // write failing, which the SDK's English strings don't describe
            // anyway, and what the user needs to know is that the pin change
            // won't survive a restart. Carrying no %1 also means R3-4's
            // planned error-code enum can't change this line.
        case "quickAccessSave":
            text = qsTr("Couldn't save your pinned folders");
            break;
            // Same no-%1 rule, same reason: the lookup was cut short (the app is
            // shutting down, or a code nothing classifies came back), so the SDK
            // string explains nothing. What matters is that the folder wasn't
            // found to be missing -- it just couldn't be checked, which is why
            // this is a toast and not the "remove this pin?" dialog.
        case "quickAccessUnavailable":
            text = qsTr("Couldn't check this folder right now — please try again");
            break;
        case "uploadNothingToUpload":
            text = qsTr("Nothing to upload — folders and non-file items can't be uploaded");
            break;
            // The uploads themselves succeeded; what needs saying is that the files
            // they were meant to replace are still there.
        case "uploadReplaceFailed":
            text = qsTr("Uploaded, but some of the files being replaced could not be removed");
            break;
        default:
            text = errorMessage;
            break;
        }
        root.push(text, "", "");
    }

    Column {
        id: column
        spacing: Theme.spacing.md

        Repeater {
            model: toastModel

            Rectangle {
                id: card

                required property int seq
                required property string text
                required property string actionText
                required property string actionPath

                property bool shown: false
                property bool expiring: false

                width: root.cardWidth
                implicitHeight: layout.implicitHeight + Theme.spacing.lg * 2
                // Animating the card's own height is what makes the stack
                // slide: the column is bottom-pinned, so a card appearing at
                // full height would shove the ones above it up in a single
                // frame. Growing from 0 moves them smoothly instead, and
                // clipping keeps the text inside while it happens.
                height: (card.shown && !card.expiring) ? card.implicitHeight : 0
                opacity: (card.shown && !card.expiring) ? 1 : 0
                clip: true

                // The chrome surface, not `surface`: the file list behind these
                // cards is `surface`, so painting them the same colour leaves a
                // 1px stroke as the only thing separating a floating card from
                // the content under it. surfaceAlt differs from the content in
                // both schemes (D3).
                color: Theme.color.surfaceAlt
                border.width: Theme.border.thin
                border.color: Theme.color.stroke
                radius: Theme.radius.md

                Behavior on height {
                    NumberAnimation {
                        duration: Theme.motion.fast
                        easing.type: Easing.OutCubic
                        // A Repeater destroys its delegate the instant the
                        // model row goes, so the row can only be removed once
                        // the fade-out has actually finished -- hence the
                        // two-phase expiring flag.
                        onFinished: if (card.expiring)
                                        root.dismiss(card.seq)
                    }
                }

                Behavior on opacity {
                    NumberAnimation {
                        duration: Theme.motion.fast
                    }
                }

                Component.onCompleted: card.shown = true

                // Swallows clicks that land on the card, which a Popup used to
                // do for free -- without this a toast is a hole you can select
                // files through. Declared before the row below, so the buttons
                // there still take their own clicks.
                MouseArea {
                    id: cardMouse
                    anchors.fill: parent
                    hoverEnabled: true
                }

                // Held off while the pointer is on the card: the close button
                // is unreachable if the card can expire on the way to it.
                Timer {
                    interval: Theme.toast.dismissMs
                    running: !card.expiring && !cardMouse.containsMouse
                    onTriggered: card.expiring = true
                }

                RowLayout {
                    id: layout
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacing.lg
                    spacing: Theme.spacing.md

                    Label {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        text: card.text
                        wrapMode: Text.Wrap
                        color: Theme.color.text
                        font.pixelSize: Theme.font.body
                    }

                    Button {
                        Layout.alignment: Qt.AlignVCenter
                        text: card.actionText
                        visible: card.actionText !== ""
                        onClicked: {
                            downloadController.openFile(card.actionPath);
                            root.dismiss(card.seq);
                        }
                    }

                    ToolButton {
                        id: closeButton
                        Layout.alignment: Qt.AlignVCenter
                        // Same squeeze as TabStrip's tab-close button: Fluent's
                        // icon-only ToolButton is 38x32, and the padding alone
                        // cannot get under the background's implicit 32, so the
                        // background is replaced too. All four paddings are
                        // spelled out because Fluent binds each side
                        // individually and beats the grouped shorthand.
                        topPadding: Theme.spacing.xs
                        bottomPadding: Theme.spacing.xs
                        leftPadding: Theme.spacing.xs
                        rightPadding: Theme.spacing.xs
                        implicitWidth: 20
                        implicitHeight: 20
                        background: Rectangle {
                            radius: Theme.radius.sm
                            color: closeButton.pressed ? Theme.color.subtlePressed :
                                                         closeButton.hovered
                                                         ? Theme.color.subtleHover : "transparent"
                        }
                        font.family: Theme.font.iconFamily
                        font.pixelSize: 10
                        text: Theme.glyph.close
                        focusPolicy: Qt.NoFocus
                        onClicked: card.expiring = true
                    }
                }
            }
        }
    }
}
