import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls

// One instance in Main.qml: a plugin's ui.alert, ui.confirm or ui.showResult, the
// oldest of pluginController.dialogRequests. Opening and closing follow that list,
// so a plugin that dies while asking takes its question with it; a result stays.
Dialog {
    id: root

    required property var plugins

    readonly property var request: root.plugins.dialogRequests.length > 0
                                   ? root.plugins.dialogRequests[0] : null
    readonly property string kind: root.request?.kind ?? ""
    readonly property bool danger: root.request?.danger ?? false
    // Guards against answering twice before the model drops the request.
    property bool answered: false
    // Two results in a row have the same kind, so seq tells the button box to reset.
    property int shownSeq: 0

    function answer(ok) {
        if (!root.request || root.answered)
            return;
        root.answered = true;
        root.plugins.answerDialog(root.request.seq, ok);
    }

    // Reads request, not kind or danger: those bindings may not have caught up yet here.
    onRequestChanged: {
        const request = root.request;
        if (request) {
            if (request.seq === root.shownSeq)
                return;
            root.shownSeq = request.seq;
            root.answered = false;
            root.standardButtons = request.kind === "confirm" ? Dialog.Ok | Dialog.Cancel
                                 : request.kind === "result" ? Dialog.Close : Dialog.Ok;
            StandardButtonLabels.pin(root.footer);
            root.styleButtons();
            resultText.text = request.kind === "result" ? request.message : "";
            root.open();
        } else {
            root.close();
        }
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    // Above PluginProgressDialog, which can open after the question does and is
    // modal too: under it the question could not be answered.
    z: 1
    modal: true
    closePolicy: Popup.CloseOnEscape
    title: root.request ? (root.request.title !== "" ? root.request.title : root.request.pluginName) : ""

    // Same cap as ConfirmPermanentDeleteDialog: a long message must wrap, not
    // widen the frame past the window. A result may be a whole report, so it gets more.
    readonly property real maxWidth: Math.min(root.kind === "result" ? 640 : 480, Overlay.overlay.width - 48)
    width: Math.min(implicitWidth, maxWidth)

    standardButtons: Dialog.Ok | Dialog.Cancel
    Component.onCompleted: {
        StandardButtonLabels.pin(footer);
        root.styleButtons();
    }

    function styleButtons() {
        const ok = root.standardButton(Dialog.Ok);
        if (!ok)
            return;
        ok.text = root.request && root.request.okLabel !== "" ? root.request.okLabel : qsTr("OK");
        const cancel = root.standardButton(Dialog.Cancel);
        // A danger question keeps the accent off the button that does the damage.
        const danger = (root.request?.danger ?? false) && cancel !== null;
        ok.highlighted = !danger;
        if (cancel)
            ok.palette.buttonText = danger ? Theme.color.danger : cancel.palette.buttonText;
    }

    // Answering the shown request makes the next one current from inside Dialog.done(),
    // which closes the dialog after its accepted/rejected handlers: open() there is undone.
    onClosed: {
        if (root.request && !root.answered)
            root.open();
    }

    onOpened: {
        root.styleButtons();
        const focus = root.standardButton(root.kind === "result" ? Dialog.Close
                                          : root.danger ? Dialog.Cancel : Dialog.Ok);
        focus?.forceActiveFocus();
    }
    // Escape on an alert still just acknowledges it, and closes a result.
    onAccepted: root.answer(true)
    onRejected: root.answer(root.kind !== "confirm")

    contentItem: Item {
        implicitWidth: root.kind === "result" ? resultView.implicitWidth : messageLabel.implicitWidth
        implicitHeight: root.kind === "result" ? resultView.implicitHeight : messageLabel.implicitHeight

        Label {
            id: messageLabel

            visible: root.kind !== "result"
            width: Math.min(implicitWidth, root.maxWidth - root.leftPadding - root.rightPadding)
            wrapMode: Text.Wrap
            text: root.kind !== "result" ? (root.request?.message ?? "") : ""
        }

        ScrollView {
            id: resultView

            visible: root.kind === "result"
            // A wrapping TextArea reports no width of its own, so the longest line is measured apart.
            implicitWidth: Math.min(resultMeasure.implicitWidth + resultText.leftPadding + resultText.rightPadding
                                    + 16, root.maxWidth - root.leftPadding - root.rightPadding)
            implicitHeight: Math.min(resultText.implicitHeight, 320)
            width: parent.width
            height: parent.height

            // Selectable, so a report can be copied out.
            TextArea {
                id: resultText

                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                textFormat: TextEdit.PlainText
                color: root.request?.level === "error" ? Theme.color.danger : palette.text
                background: null
            }
        }

        Text {
            id: resultMeasure

            visible: false
            font: resultText.font
            textFormat: Text.PlainText
            text: resultText.text
        }
    }
}
