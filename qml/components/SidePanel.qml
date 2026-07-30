import QtQuick
import QtQuick.Layouts

// No QtQuick.Controls.FluentWinUI3 import, unlike its siblings in this
// directory: this file instantiates no Controls type of its own (only a
// ColumnLayout, a Rectangle and the two panel components below), and qmllint
// flags the import as unused. The style still applies -- the files that do use
// Controls types import it themselves.

// The whole left side panel: Phase 11's quick-access pins stacked above Phase
// 10's folder tree, matching Explorer's ordering.
//
// Exists purely as a wrapper so FolderTreePanel.qml can stay a bare TreeView
// with its Phase 10 design notes intact. Main.qml's SplitView now holds this
// item, which means SplitView's attached size properties and the persisted-
// width one-shot read moved here too.
ColumnLayout {
    id: root

    required property var navController

    spacing: 0

    SystemPalette {
        id: sysPalette
    }

    QuickAccessSection {
        Layout.fillWidth: true
        navController: root.navController
        // This panel's height is set by SplitView, independent of its own
        // contents, so it's safe for the section to cap itself against it.
        availableHeight: root.height
    }

    // Only drawn when there's a pin section above it to separate from.
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        visible: quickAccessModel.count > 0
        color: sysPalette.mid
    }

    FolderTreePanel {
        Layout.fillWidth: true
        Layout.fillHeight: true
        navController: root.navController
    }
}
