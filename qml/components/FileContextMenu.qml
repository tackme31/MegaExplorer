import QtQuick
import QtQuick.Controls.FluentWinUI3

// Extracted out of Main.qml's former inline `component FileContextMenu`
// (Phase 6b): an inline component is scoped to its own file, so it couldn't
// be referenced from the new qml/views/FileTableView.qml. Shared by both the
// grid view's delegate (Main.qml) and the table view's delegate
// (FileTableView.qml).
Menu {
    required property var delegateItem

    MenuItem {
        text: qsTr("Download")
        onTriggered: downloadController.downloadFile(delegateItem.handle, delegateItem.name,
                                                     delegateItem.sizeBytes)
    }
}
