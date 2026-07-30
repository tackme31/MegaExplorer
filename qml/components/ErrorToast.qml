import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml/DownloadSnackbar.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// Generic auto-hiding error notification for controllers with no dedicated
// UI feedback path (folder navigation, search, thumbnail fetch, and
// DownloadController's openFile OS-failure sub-path) -- fed by
// notificationController.errorOccurred(context, errorMessage). context
// selects the localized sentence template; mirrors DownloadSnackbar's own
// show(...)/structured-fields convention rather than receiving a
// pre-formatted string. DownloadSnackbar itself is untouched by this --
// the main download success/fail flow keeps its own dedicated popup.
Popup {
    id: root

    property string context: ""
    property string errorMessage: ""

    function show(ctx, message) {
        context = ctx;
        errorMessage = message;
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
            case "navigation":
                return qsTr("Failed to load folder: %1").arg(root.errorMessage);
            case "search":
                return qsTr("Search failed: %1").arg(root.errorMessage);
            case "thumbnail":
                return qsTr("Failed to load thumbnail: %1").arg(root.errorMessage);
            case "openFile":
                return qsTr("Failed to open file: %1").arg(root.errorMessage);
            case "rename":
                return qsTr("Failed to rename: %1").arg(root.errorMessage);
            default:
                return root.errorMessage;
            }
        }
    }
}
