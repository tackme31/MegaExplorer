import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// Bottom-anchored, auto-hiding notification shown once per finished download
// (success or failure -- see show()). Success shows an "開く" button wired to
// DownloadController::openFile(); failure shows only the error message, no
// button, mirroring the "no auto-open" rule this feature is built around.
Popup {
    id: root

    property bool success: true
    property string fileName: ""
    property string localPath: ""
    property string errorMessage: ""

    function show(ok, name, path, error) {
        success = ok;
        fileName = name;
        localPath = path;
        errorMessage = error;
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

    contentItem: RowLayout {
        spacing: 12

        Label {
            Layout.fillWidth: true
            Layout.maximumWidth: 320
            wrapMode: Text.Wrap
            text: root.success ? qsTr("%1 のダウンロードが完了しました").arg(root.fileName) : qsTr(
                                     "%1 のダウンロードに失敗しました: %2").arg(root.fileName).arg(
                                     root.errorMessage)
        }

        Button {
            text: qsTr("開く")
            visible: root.success
            onClicked: {
                downloadController.openFile(root.localPath);
                root.close();
            }
        }
    }
}
