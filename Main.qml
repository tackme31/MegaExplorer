import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls").
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

ApplicationWindow {
    id: window
    width: 640
    height: 480
    minimumWidth: 200
    minimumHeight: 250
    visible: true
    title: qsTr("MegaExplorer")

    header: ToolBar {
        RowLayout {
            anchors.fill: parent

            ToolButton {
                text: qsTr("← Back")
                enabled: controller.canGoBack
                onClicked: controller.goBack()
            }

            TextField {
                Layout.fillWidth: true
                placeholderText: qsTr("Search in this folder")
                // MegaApi::search() blocks the GUI thread synchronously, so
                // search on Enter only rather than on every keystroke.
                onAccepted: controller.search(text)
            }
        }
    }

    footer: ToolBar {
        visible: downloadController.downloadActive
        RowLayout {
            anchors.fill: parent
            Label {
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: downloadController.activeFileName
            }
            ProgressBar {
                Layout.preferredWidth: 160
                from: 0
                to: 1
                value: downloadController.activeProgress
            }
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
            required property var sizeBytes

            width: ListView.view.width
            text: (isFolder ? "📁 " : "📄 ") + name

            TapHandler {
                acceptedButtons: Qt.LeftButton
                onDoubleTapped: {
                    if (delegateItem.isFolder)
                        controller.openFolder(delegateItem.handle);
                    else
                        downloadController.downloadFile(delegateItem.handle, delegateItem.name,
                                                        delegateItem.sizeBytes);
                }
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: if (!delegateItem.isFolder)
                              contextMenu.popup()
            }

            Menu {
                id: contextMenu
                MenuItem {
                    text: qsTr("ダウンロード")
                    onTriggered: downloadController.downloadFile(delegateItem.handle,
                                                                 delegateItem.name,
                                                                 delegateItem.sizeBytes)
                }
            }
        }
    }
}
