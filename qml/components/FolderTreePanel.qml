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
    // Main.qml's window-wide DragProxy. This panel is a drop target only --
    // dragging *out* of the tree is deliberately not supported (Phase 14a), so
    // no DragHandler appears below.
    required property var dragProxy

    clip: true

    model: folderTreeModel

    SystemPalette {
        id: sysPalette
    }

    // One instance for the whole tree rather than one per delegate row (Phase
    // 13b's lesson). Menu is a Popup, not an Item, so it's neither laid out by
    // this TreeView nor clipped by its Flickable viewport.
    FolderPinMenu {
        id: pinMenu
    }

    // Driven from the per-delegate DropAreas below. A stationary cursor at the
    // panel edge keeps scrolling because content moving underneath doesn't
    // re-deliver drag events -- the last track() call stands until the pointer
    // actually moves again, or the gesture ends and onExited releases it.
    DragAutoScroller {
        id: autoScroller
        flickable: root
    }

    delegate: TreeViewDelegate {
        id: treeDelegate

        // row is not redeclared here: TreeViewDelegate's own style
        // implementation already declares it as a required property, and
        // redeclaring shadows it with a second, never-populated copy.
        required property var handle
        required property bool isRoot
        // Only needed to label a new pin; the delegate's own text still comes
        // from TreeViewDelegate's stock contentItem reading Qt::DisplayRole.
        required property string name

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
            // Outlined rather than filled, so the drop target stays legible on
            // the row that also happens to be the current folder.
            border.width: dropArea.accepting ? 2 : 0
            border.color: sysPalette.highlight
        }

        // Per delegate, unlike the file views' single view-level DropArea: a
        // tree row *is* the drop target, so there's no position-to-row
        // hit-testing to centralize, and the existing handlers here are already
        // per delegate. isRoot rows are valid targets too -- dropping on
        // "Cloud Drive" moves to the account root.
        DropArea {
            id: dropArea
            anchors.fill: parent
            keys: ["application/x-megaexplorer-nodes"]

            // Recomputed on enter only: the target can't change without leaving
            // this row first, so a positionChanged handler would re-ask the SDK
            // for an answer that cannot have changed.
            property bool accepting: false

            // Payload read off root.dragProxy rather than the event's own
            // drag.source: same object (keys let nothing else in), but
            // drag.source is typed QObject and every field access through it
            // would be an unchecked dynamic lookup.
            onEntered: drag => {
                dropArea.accepting = root.dragProxy.sourceNav.canDropHandlesOn(
                            root.dragProxy.handles, treeDelegate.handle, treeDelegate.isRoot);
                autoScroller.track(root.mapFromItem(dropArea, drag.x, drag.y).y);
            }
            onPositionChanged: drag => autoScroller.track(
                                   root.mapFromItem(dropArea, drag.x, drag.y).y)
            onExited: {
                dropArea.accepting = false;
                autoScroller.release();
            }
            onDropped: {
                if (dropArea.accepting)
                    root.dragProxy.sourceNav.moveHandlesTo(root.dragProxy.handles,
                                                           treeDelegate.handle,
                                                           treeDelegate.isRoot);
                dropArea.accepting = false;
                autoScroller.release();
            }
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

        // No select-then-popup step here, unlike the file views: the tree has
        // no selectionModel at all (its highlight follows navigation state), so
        // the clicked row is handed to the menu directly.
        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: pinMenu.popupFor(treeDelegate.handle, treeDelegate.isRoot, treeDelegate.name)
        }
    }
}
