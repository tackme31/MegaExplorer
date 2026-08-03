import QtQuick
import QtQuick.Layouts

// No QtQuick.Controls.FluentWinUI3 import, unlike its siblings in this
// directory: this file instantiates no Controls type of its own (only
// Rectangles, a ColumnLayout and the two panel components below), and qmllint
// flags the import as unused. The style still applies -- the files that do use
// Controls types import it themselves.

// The whole left side panel: Phase 11's quick-access pins stacked above Phase
// 10's folder tree, matching Explorer's ordering.
//
// Exists purely as a wrapper so FolderTreePanel.qml can stay a bare TreeView
// with its Phase 10 design notes intact. Main.qml's SplitView now holds this
// item, which means SplitView's attached size properties and the persisted-
// width one-shot read moved here too.
// A Rectangle rather than the bare ColumnLayout it used to be: D3 puts this
// panel on the lighter surface of Explorer 11's two-surface split, and a
// layout cannot paint one.
Rectangle {
    id: root

    required property var navController
    // Main.qml's single window-wide DragProxy -- both halves of this panel are
    // drop targets for a move drag started in a file view.
    required property var dragProxy

    color: Theme.color.surfaceAlt

    // The single hover source behind D7's "chevrons only while the pointer is
    // in the pane". One handler for the whole panel, never one per row: see
    // FolderTreePanel.qml's paneHovered for what a row-level version breaks.
    // Explorer scopes this the same way -- hovering the quick-access half also
    // brings out the tree's chevrons.
    HoverHandler {
        id: paneHover
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        QuickAccessSection {
            Layout.fillWidth: true
            navController: root.navController
            dragProxy: root.dragProxy
            // This panel's height is set by SplitView, independent of its own
            // contents, so it's safe for the section to cap itself against it.
            availableHeight: root.height
        }

        // Only drawn when there's a pin section above it to separate from.
        // Full `stroke`, despite dividing two sections of one surface rather
        // than two surfaces: measured against surfaceAlt, anything weaker
        // lands under 6/255 and stays as invisible as the SystemPalette.mid
        // this replaces (3-7).
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.border.thin
            // The tree used to start on the very next row (3-7).
            Layout.topMargin: Theme.spacing.sm
            Layout.bottomMargin: Theme.spacing.sm
            visible: quickAccessModel.count > 0
            color: Theme.color.stroke
        }

        FolderTreePanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            navController: root.navController
            dragProxy: root.dragProxy
            paneHovered: paneHover.hovered
        }
    }
}
