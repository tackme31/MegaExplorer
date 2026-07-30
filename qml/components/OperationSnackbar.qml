import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml/DownloadSnackbar.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// Auto-hiding outcome notification for bulk operations, fed by
// notificationController.operationFinished(context, succeeded, failed) --
// DownloadSnackbar.qml's shape minus its "Open" button, since none of these
// operations produces anything to open. context selects the sentence and the
// counts are plugged into it, same structured-fields convention as
// ErrorToast.qml.
//
// No Undo button: reversing a Rubbish-bin move would need a general move
// operation, which IMegaClient deliberately doesn't expose yet (see
// docs/PROGRESS.md's Phase 12 log).
Popup {
    id: root

    property string context: ""
    property int succeeded: 0
    property int failed: 0

    function show(ctx, succeededCount, failedCount) {
        context = ctx;
        succeeded = succeededCount;
        failed = failedCount;
        open();
        hideTimer.restart();
    }

    x: (parent ? (parent.width - width) / 2 : 0)
    y: (parent ? parent.height - height - 16 : 0)
    modal: false
    focus: false
    closePolicy: Popup.CloseOnEscape

    Timer {
        id: hideTimer
        interval: 6000
        onTriggered: root.close()
    }

    contentItem: Label {
        Layout.maximumWidth: 320
        wrapMode: Text.Wrap
        text: {
            switch (root.context) {
            case "moveToRubbish":
                if (root.failed === 0)
                    return qsTr("Moved %1 item(s) to the Rubbish bin").arg(root.succeeded);
                if (root.succeeded === 0)
                    return qsTr("Failed to move %1 item(s) to the Rubbish bin").arg(root.failed);
                return qsTr("Moved %1 item(s) to the Rubbish bin, %2 failed").arg(
                            root.succeeded).arg(root.failed);
            default:
                return "";
            }
        }
    }
}
