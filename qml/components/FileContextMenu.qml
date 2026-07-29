import QtQuick
import QtQuick.Controls.FluentWinUI3

// Extracted out of Main.qml's former inline `component FileContextMenu`
// (Phase 6b): an inline component is scoped to its own file, so it couldn't
// be referenced from the new qml/views/FileTableView.qml. Shared by both the
// grid view's delegate (Main.qml) and the table view's delegate
// (FileTableView.qml).
Menu {
    required property var delegateItem

    // Menu's default contentItem is a ListView, which sizes each row off its
    // height rather than its visibility -- an invisible-but-nonzero-height
    // MenuItem still reserves its row, so the popup shows a blank line
    // instead of shrinking. Collapse height to 0 alongside visible so the
    // hidden item is excluded from the layout, not just unpainted.
    MenuItem {
        text: qsTr("Download")
        visible: !delegateItem.isFolder
        height: visible ? implicitHeight : 0
        onTriggered: downloadController.downloadFile(delegateItem.handle, delegateItem.name,
                                                     delegateItem.sizeBytes)
    }

    // Folders have no actions yet (delete/rename etc. are future phases) -- show a
    // disabled placeholder rather than silently refusing to open the menu at all.
    MenuItem {
        text: qsTr("None")
        visible: delegateItem.isFolder
        height: visible ? implicitHeight : 0
        enabled: false
    }
}
