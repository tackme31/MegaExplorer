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

    // alreadyPresent means the SDK skipped the transfer because an identical
    // file was already there -- without saying so the generic "completed"
    // message is indistinguishable from an overwrite.
    //
    // The failure branch names the file and stops there: the SDK's reason for
    // it is an English sentence that never gets translated, and it stays in
    // DownloadController's qCWarning (R5-10).
    function describeDownload(success, fileName, alreadyPresent) {
        if (!success)
            return qsTr("Couldn't download %1").arg(fileName);
        if (alreadyPresent)
            return qsTr("%1 is already downloaded").arg(fileName);
        return qsTr("%1 downloaded").arg(fileName);
    }

    // Success gets an "Open" button wired to DownloadController::openFile();
    // failure shows only the message, mirroring the "no auto-open" rule this
    // feature is built around.
    function showDownload(success, fileName, localPath, alreadyPresent) {
        const text = root.describeDownload(success, fileName, alreadyPresent);
        root.push(text, success ? qsTr("Open") : "", localPath);
    }

    // Returns "" for a context with no case, which showOperation below reads as
    // "say nothing". Unlike describeError's default that is not a gap to warn
    // about: C++ only emits operationFinished for the contexts listed here.
    function describeOperation(context, succeeded, failed) {
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
        return text;
    }

    // Bulk-operation outcomes, fed by
    // notificationController.operationFinished(context, succeeded, failed).
    // No Undo button: a Rubbish-bin undo is finally expressible since Phase
    // 14a added a general move, but it would need the pre-move parent of every
    // item in the batch, which nothing records.
    function showOperation(context, succeeded, failed) {
        const text = root.describeOperation(context, succeeded, failed);
        if (text !== "")
            root.push(text, "", "");
    }

    // Appends the reason C++ folded the errorCode into to the clause naming
    // what failed. Composing from two translated fragments rather than
    // spelling out every context x reason sentence keeps the string count at
    // 8 + 4 instead of 32; if the seam reads badly in a target language, R6
    // can open the affected contexts out into full sentences without any C++
    // change, since the reason is a value and not a piece of text.
    //
    // Unknown is the only branch that shows rawMessage, and NotificationController
    // is the only thing that decides when that is. Since R5-10 it is also the
    // only place in the app where an SDK English sentence can still surface --
    // the login screen and describeDownload above both dropped theirs.
    function describeReason(clause, reason, rawMessage) {
        switch (reason) {
        case NotificationController.NotFound:
            return qsTr("%1 — it no longer exists").arg(clause);
        case NotificationController.NoPermission:
            return qsTr("%1 — you don't have permission").arg(clause);
        case NotificationController.Offline:
            return qsTr("%1 — check your connection").arg(clause);
        default:
            return rawMessage ? qsTr("%1: %2").arg(clause).arg(rawMessage) : clause;
        }
    }

    // context names the operation, reason says why; no English from the SDK
    // reaches this function except as rawMessage on an Unknown reason.
    //
    // The cases split in two: those that pass through describeReason() because
    // an errorCode existed, and those with a fixed sentence because the
    // failure never reached the SDK and so has no code to classify. The second
    // group ignores both reason and rawMessage by design -- C++ sends them
    // Unknown and "".
    function describeError(context, reason, rawMessage) {
        let text;
        switch (context) {
        case "navigation":
            text = root.describeReason(qsTr("Failed to load folder"), reason, rawMessage);
            break;
        case "search":
            text = root.describeReason(qsTr("Search failed"), reason, rawMessage);
            break;
        case "thumbnail":
            text = root.describeReason(qsTr("Failed to load thumbnail"), reason, rawMessage);
            break;
        case "rename":
            text = root.describeReason(qsTr("Failed to rename"), reason, rawMessage);
            break;
        case "createFolder":
            text = root.describeReason(qsTr("Failed to create folder"), reason, rawMessage);
            break;
            // The whole paste was refused before anything was attempted (a
            // read-only share, a destination that vanished). Per-item failures
            // never come here -- they land in the "copy"/"move" tally above.
        case "paste":
            text = root.describeReason(qsTr("Can't paste here"), reason, rawMessage);
            break;
            // Ctrl+drop's counterpart of "paste" above, and the same rule: only
            // whole-drop refusals land here (a read-only destination, a folder
            // that vanished, a destination listing that couldn't be read). The
            // per-item tally is the "copy" case in showOperation.
        case "copy":
            text = root.describeReason(qsTr("Can't copy here"), reason, rawMessage);
            break;
            // The background sync behind a manual refresh. Missing until R3-5:
            // it fell to default: below and put a bare "Internal error" on
            // screen, with no sentence and no qsTr around it.
        case "refresh":
            text = root.describeReason(qsTr("Couldn't refresh this folder"), reason, rawMessage);
            break;
            // Fixed sentence: QDesktopServices::openUrl reports nothing but
            // false, so there is no code to classify. The path it failed on is
            // in DownloadController's log, not here -- it is the file the user
            // just asked for.
        case "openFile":
            text = qsTr("Couldn't open this file");
            break;
            // Fixed sentence: the name was rejected by FileOperationService's
            // own validation before the SDK was reached, so no code exists. The
            // reason is spelled out because, unlike NewFolderDialog's inline
            // message, a toast appears with no name field next to it.
        case "renameInvalidName":
            text = qsTr("That name can't be used — names can't be empty or contain \\ or /");
            break;
            // Fixed sentence: the cause is a local settings write failing, not
            // anything the SDK saw, and what the user needs to know is that the
            // pin change won't survive a restart.
        case "quickAccessSave":
            text = qsTr("Couldn't save your pinned folders");
            break;
            // Fixed sentence for the opposite reason -- a code did come back,
            // but the lookup was cut short (the app is shutting down, or
            // nothing classified it), so it says nothing worth relaying. What
            // matters is that the folder wasn't found to be missing, it just
            // couldn't be checked, which is why this is a toast and not the
            // "remove this pin?" dialog.
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
            // A context C++ sends that nothing here handles -- exactly what
            // R3-5 was. The old default printed rawMessage, which meant the
            // gap surfaced as untranslated English to the user and to nobody
            // else; warn instead, so it shows up while developing.
        default:
            console.warn("ToastStack: no case for error context", context);
            text = root.describeReason(qsTr("Something went wrong"), reason, rawMessage);
            break;
        }
        return text;
    }

    // Errors from controllers with no dedicated feedback path, fed by
    // notificationController.errorOccurred(context, reason, rawMessage).
    function showError(context, reason, rawMessage) {
        root.push(root.describeError(context, reason, rawMessage), "", "");
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
