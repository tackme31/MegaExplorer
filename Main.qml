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

    ListView {
        anchors.fill: parent
        model: fileListModel
        clip: true

        delegate: ItemDelegate {
            required property string name
            required property bool isFolder

            width: ListView.view.width
            text: (isFolder ? "📁 " : "📄 ") + name
        }
    }
}
