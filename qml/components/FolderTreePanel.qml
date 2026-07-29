import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml/Breadcrumb.qml/FileTableView.qml.
import QtQuick.Controls.FluentWinUI3

// Left side-panel folder tree (Phase 10). model is folderTreeModel, an
// app-lifetime context property shared across every tab (main.cpp) -- but
// navController is swapped out by Main.qml to always be the *active* tab's
// FolderNavigationController, so only the highlight follows a tab switch;
// the tree's own expansion state is untouched.
//
// Explorer semantics -- the chevron expands/collapses, the label navigates --
// come from TreeViewDelegate's stock pointer handling, which already toggles
// expansion only for clicks that land on the indicator. Do NOT set
// pointerNavigationEnabled: false to "take over" that behavior: it kills the
// indicator's expand/collapse outright, and a replacement TapHandler placed
// on a custom indicator: item never fires (the delegate consumes the press
// first), leaving the chevron completely dead. The stock indicator also keeps
// its automatic indentation, which a custom one loses.
//
// The delegate-level TapHandler below therefore only ever sees clicks outside
// the indicator, which is exactly the "label navigates" half.
TreeView {
    id: root

    required property var navController

    clip: true

    model: folderTreeModel

    SystemPalette {
        id: sysPalette
    }

    delegate: TreeViewDelegate {
        id: treeDelegate

        // row is not redeclared here: TreeViewDelegate's own style
        // implementation already declares it as a required property, and
        // redeclaring shadows it with a second, never-populated copy.
        required property var handle
        required property bool isRoot

        // Matches TabStrip.qml's TabButton: without this, clicking a row
        // strands keyboard focus here instead of the file view, deadening
        // its arrow-key navigation until the view is re-clicked.
        focusPolicy: Qt.NoFocus

        readonly property bool isCurrent: root.navController ? (treeDelegate.isRoot
                                                                ? root.navController.atRoot : (
                                                                      !root.navController.atRoot
                                                                      && treeDelegate.handle
                                                                      === root.navController.currentHandle)) :
                                                               false

        // No selectionModel is set on the TreeView -- the highlight follows
        // navigation state, not a click-driven selection -- so the style's
        // own highlighted/selected painting is replaced wholesale here.
        // implicitHeight restates what the style's background supplies:
        // without it, rows whose indicator is hidden (a folder already known
        // to be empty) would come out shorter than the rest.
        background: Rectangle {
            implicitHeight: 24
            color: treeDelegate.isCurrent ? Qt.rgba(sysPalette.highlight.r, sysPalette.highlight.g, sysPalette.highlight.b,
                                                    0.35) : "transparent"
        }

        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: root.navController?.navigateTo(treeDelegate.handle, treeDelegate.isRoot)
        }

        // Mirrors the file views' middle-click "open in new tab" convention
        // (FileGridView.qml/FileTableView.qml) -- background tab, current
        // tab keeps focus.
        TapHandler {
            acceptedButtons: Qt.MiddleButton
            onTapped: tabsController.addTabAt(treeDelegate.handle, treeDelegate.isRoot)
        }
    }
}
