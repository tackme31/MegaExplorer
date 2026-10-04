import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls

// One instance in Main.qml: a plugin's ui.confirm. Shows the oldest open question;
// opening and closing follow pluginController.confirmRequests, so a plugin that
// dies while asking takes its dialog with it.
Dialog {
    id: root

    required property var plugins

    readonly property var request: root.plugins.confirmRequests.length > 0
                                   ? root.plugins.confirmRequests[0] : null
    readonly property bool danger: root.request?.danger ?? false
    // Guards against answering twice before the model drops the question.
    property bool answered: false

    function answer(ok) {
        if (!root.request || root.answered)
            return;
        root.answered = true;
        root.plugins.answerConfirm(root.request.pluginId, ok);
    }

    onRequestChanged: {
        if (root.request) {
            root.answered = false;
            root.styleButtons();
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
    title: root.request ? (root.request.title !== "" ? root.request.title :
                                                       root.request.pluginName) : ""

    // Same cap as ConfirmPermanentDeleteDialog: a long message must wrap, not
    // widen the frame past the window.
    readonly property real maxWidth: Math.min(480, Overlay.overlay.width - 48)
    width: Math.min(implicitWidth, maxWidth)

    // standardButtons rather than a hand-built box: the style lays the buttons out at
    // equal width only when they are all there from the start.
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
        // A danger question keeps the accent off the button that does the damage.
        ok.highlighted = !root.danger;
        ok.palette.buttonText = root.danger ? Theme.color.danger : root.standardButton(Dialog.Cancel).palette.buttonText;
    }

    onOpened: {
        root.styleButtons();
        root.standardButton(root.danger ? Dialog.Cancel : Dialog.Ok).forceActiveFocus();
    }
    onAccepted: root.answer(true)
    onRejected: root.answer(false)

    Label {
        width: Math.min(implicitWidth, root.maxWidth - root.leftPadding - root.rightPadding)
        wrapMode: Text.Wrap
        text: root.request?.message ?? ""
    }
}
