import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls").
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QtCore
// Directory import for DownloadSnackbar.qml -- the CMake-generated qmldir
// merge (QTP0004) resolves this at build time regardless, but static tooling
// (Qt Creator's classic QML/JS model, qmllint without the build dir) only
// knows about the plain-QML directory-import mechanism, not that mechanism.
import "components"

ApplicationWindow {
    id: window
    width: 640
    height: 480
    minimumWidth: 200
    minimumHeight: 250
    visible: true
    title: qsTr("MegaExplorer")

    // 0 = list, 1 = grid. Persisted below via Settings (alias, so every
    // change is written through automatically -- a plain property on
    // Settings would only capture the value at startup).
    property int viewMode: 0

    // Shared list/grid right-click menu. Inline components must be declared
    // inside the root object, not as a top-level sibling before it -- the
    // latter is a syntax error (qmlcachegen rejects it even though some
    // examples elsewhere show it at file scope).
    component FileContextMenu: Menu {
        required property var delegateItem

        MenuItem {
            text: qsTr("ダウンロード")
            onTriggered: downloadController.downloadFile(delegateItem.handle, delegateItem.name,
                                                         delegateItem.sizeBytes)
        }
    }

    Settings {
        property alias viewMode: window.viewMode
    }

    function activateEntry(isFolder, handle, name, sizeBytes) {
        if (isFolder)
            controller.openFolder(handle);
        else
            downloadController.downloadFile(handle, name, sizeBytes);
    }

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
        RowLayout {
            anchors.fill: parent

            Label {
                Layout.fillWidth: true
                visible: downloadController.downloadActive
                elide: Text.ElideMiddle
                text: downloadController.activeFileName
            }
            ProgressBar {
                Layout.preferredWidth: 160
                visible: downloadController.downloadActive
                from: 0
                to: 1
                value: downloadController.activeProgress
            }

            // Keeps the view-mode buttons right-aligned when the download
            // group above is hidden (RowLayout excludes invisible items).
            Item {
                Layout.fillWidth: true
                visible: !downloadController.downloadActive
            }

            ToolButton {
                text: "☰"
                checkable: true
                checked: window.viewMode === 0
                onClicked: window.viewMode = 0
            }
            ToolButton {
                text: "⊞"
                checkable: true
                checked: window.viewMode === 1
                onClicked: window.viewMode = 1
            }
        }
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: window.viewMode

        ListView {
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
                    onDoubleTapped: window.activateEntry(delegateItem.isFolder, delegateItem.handle,
                                                         delegateItem.name, delegateItem.sizeBytes)
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: if (!delegateItem.isFolder)
                                  contextMenu.popup()
                }

                FileContextMenu {
                    id: contextMenu
                    delegateItem: delegateItem
                }
            }
        }

        GridView {
            model: controller.fileListModel
            clip: true
            cellWidth: 120
            cellHeight: 120

            delegate: Item {
                id: gridDelegateItem
                required property string name
                required property bool isFolder
                required property var handle
                required property var sizeBytes
                required property bool hasThumbnail
                required property string thumbnailPath

                width: GridView.view.cellWidth
                height: GridView.view.cellHeight

                Component.onCompleted: {
                    if (gridDelegateItem.hasThumbnail && !gridDelegateItem.isFolder)
                        thumbnailController.requestThumbnail(gridDelegateItem.handle);
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 4
                    spacing: 2

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        Image {
                            anchors.fill: parent
                            visible: gridDelegateItem.hasThumbnail && !gridDelegateItem.isFolder
                                     && gridDelegateItem.thumbnailPath !== ""
                            // thumbnailPath uses native (backslash-on-Windows)
                            // separators -- normalize before building a URL.
                            source: gridDelegateItem.thumbnailPath ? ("file:///"
                                                                      + gridDelegateItem.thumbnailPath.replace(
                                                                          /\\/g, "/")) : ""
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: !gridDelegateItem.hasThumbnail || gridDelegateItem.isFolder
                                     || gridDelegateItem.thumbnailPath === ""
                            text: gridDelegateItem.isFolder ? "📁" : "📄"
                            font.pixelSize: 32
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideMiddle
                        text: gridDelegateItem.name
                    }
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onDoubleTapped: window.activateEntry(gridDelegateItem.isFolder,
                                                         gridDelegateItem.handle,
                                                         gridDelegateItem.name,
                                                         gridDelegateItem.sizeBytes)
                }

                TapHandler {
                    acceptedButtons: Qt.RightButton
                    onTapped: if (!gridDelegateItem.isFolder)
                                  gridContextMenu.popup()
                }

                FileContextMenu {
                    id: gridContextMenu
                    delegateItem: gridDelegateItem
                }
            }
        }
    }

    DownloadSnackbar {
        id: downloadSnackbar
        parent: Overlay.overlay
    }

    Connections {
        target: downloadController
        function onDownloadFinished(success, fileName, localPath, errorMessage, alreadyPresent) {
            downloadSnackbar.show(success, fileName, localPath, errorMessage, alreadyPresent);
        }
    }
}
