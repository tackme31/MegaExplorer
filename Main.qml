import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls").
import QtQuick.Controls.FluentWinUI3

ApplicationWindow {
    id: window
    width: 640
    height: 480
    minimumWidth: 200
    minimumHeight: 250
    visible: true
    title: qsTr("MegaExplorer")

    header: ToolBar {
        ToolButton {
            text: qsTr("← Back")
            enabled: controller.canGoBack
            onClicked: controller.goBack()
        }
    }

    ListView {
        anchors.fill: parent
        model: controller.fileListModel
        clip: true

        delegate: ItemDelegate {
            id: delegateItem
            required property string name
            required property bool isFolder
            required property var handle

            width: ListView.view.width
            text: (isFolder ? "📁 " : "📄 ") + name

            TapHandler {
                onDoubleTapped: if (delegateItem.isFolder) controller.openFolder(delegateItem.handle)
            }
        }
    }
}
