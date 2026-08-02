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

    // Pins every row to the viewport width instead of letting
    // TreeViewDelegate size itself by its own content. Two separate defects
    // come from that default:
    //
    //  - the row background (the selection highlight below) stretched only as
    //    far as the widest row's content, so how much of a selected row got
    //    painted depended on what else happened to be expanded;
    //  - the stock contentItem already asks for elide: Text.ElideRight, but a
    //    label handed exactly the width it asked for never has anything to
    //    elide, so deep rows were cut mid-glyph at the panel edge with no "…".
    //
    // Fixing the width closes both, and leaves nothing to scroll horizontally
    // -- which Explorer's navigation pane doesn't offer either.
    columnWidthProvider: function (column) {
        return root.width;
    }
    // columnWidthProvider is only consulted during a layout pass, and a width
    // change alone doesn't trigger one.
    onWidthChanged: root.forceLayout()

    // Flickable defaults acceptedButtons to Qt.LeftButton, i.e. click-dragging
    // a row pans the tree -- which the two file views already suppress
    // (FileTableView.qml, FileGridView.qml) and Explorer's navigation pane
    // doesn't do. Wheel scrolling is unaffected.
    acceptedButtons: Qt.NoButton

    // Without this there is no indication that a tree taller than the panel
    // can be scrolled at all.
    ScrollBar.vertical: ScrollBar {}

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
        //
        // Nothing about the row's *height* can be said from here: the
        // TreeViewDelegate this resolves to is Basic's (FluentWinUI3 ships no
        // TreeViewDelegate of its own), and its implicitHeight formula --
        // max(indicator.height, implicitContentHeight) * 1.25 -- has no
        // background term at all. An implicitHeight written here is simply
        // ignored; the 40px click area of the stock indicator is what sets
        // the row height.
        background: Rectangle {
            color: treeDelegate.isCurrent ? Qt.rgba(sysPalette.highlight.r, sysPalette.highlight.g, sysPalette.highlight.b,
                                                    0.35) : "transparent"
            // Outlined rather than filled, so the drop target stays legible on
            // the row that also happens to be the current folder.
            border.width: dropArea.accepting ? 2 : 0
            border.color: sysPalette.highlight
        }

        // Restated from Basic's own contentItem purely to hang a tooltip off
        // it. Now that rows are pinned to the viewport width, a deep row's
        // name really does elide -- and the panel offers no other way to read
        // it: there is no horizontal scroll, and MEGA folder names run long.
        // The stock highlighted/highlightedText branch is dropped because
        // highlighted needs a selectionModel, which this TreeView has none of.
        contentItem: Label {
            id: nameLabel
            text: treeDelegate.name
            elide: Text.ElideRight
            color: treeDelegate.palette.buttonText
            visible: !treeDelegate.editing

            ToolTip.text: treeDelegate.name
            ToolTip.delay: 500
            ToolTip.visible: treeDelegate.hovered && nameLabel.truncated
        }

        // Per delegate, unlike the file views' single view-level DropArea: a
        // tree row *is* the drop target, so there's no position-to-row
        // hit-testing to centralize, and the existing handlers here are already
        // per delegate. isRoot rows are valid targets too -- dropping on
        // "Cloud Drive" moves to the account root.
        DropArea {
            id: dropArea
            anchors.fill: parent
            // "text/uri-list" is what an external OS drop matches on: an
            // internal Qt drag is matched against Drag.keys, but a drop coming
            // in from Explorer is matched against its QMimeData's format
            // strings, and without this one those drops are silently ignored.
            keys: ["application/x-megaexplorer-nodes", "text/uri-list"]

            // Recomputed on enter only: the target can't change without leaving
            // this row first, so a positionChanged handler would re-ask the SDK
            // for an answer that cannot have changed.
            property bool accepting: false

            // Internal vs. external is decided on root.dragProxy.active, not on
            // drag.hasUrls: hasUrls is a claim about the *event*, while active
            // is a claim about the very object the internal branch then
            // dereferences. DragProxy.begin() sets both active and sourceNav,
            // and finish() calls Drag.drop() before clearing sourceNav, so it's
            // still valid inside onDropped.
            //
            // Payload read off root.dragProxy rather than the event's own
            // drag.source: same object (keys let nothing else in), but
            // drag.source is typed QObject and every field access through it
            // would be an unchecked dynamic lookup.
            onEntered: drag => {
                if (root.dragProxy.active) {
                    dropArea.accepting = root.dragProxy.sourceNav.canDropHandlesOn(
                                root.dragProxy.handles, treeDelegate.handle, treeDelegate.isRoot);
                } else if (drag.hasUrls) {
                    dropArea.accepting = uploadController.canUploadTo(treeDelegate.handle,
                                                                      treeDelegate.isRoot);
                    // Only the external branch touches drag.accepted; the move
                    // path relies on implicit acceptance via key matching, and
                    // assigning here would break it.
                    drag.accepted = dropArea.accepting;
                } else {
                    dropArea.accepting = false;
                }
                // Outside the branch: edge scrolling should work for either.
                autoScroller.track(root.mapFromItem(dropArea, drag.x, drag.y).y);
            }
            onPositionChanged: drag => {
                if (!root.dragProxy.active && drag.hasUrls)
                    drag.accepted = dropArea.accepting;
                autoScroller.track(root.mapFromItem(dropArea, drag.x, drag.y).y);
            }
            onExited: {
                dropArea.accepting = false;
                autoScroller.release();
            }
            // Branches on drop.hasUrls, not on dragProxy.active like the hover
            // handlers above: DragProxy.finish() calls Drag.drop() to deliver
            // this very event, and Drag.active is cleared as a side effect of
            // that same call, so its value here depends on Qt's internal
            // ordering. The event's own payload doesn't.
            //
            // A row whose folder is gone is rejected by canDropHandlesOn's
            // kENoEnt on the move path, and by canUploadTo's on the upload one.
            onDropped: drop => {
                if (dropArea.accepting) {
                    if (drop.hasUrls) {
                        drop.accept(Qt.CopyAction);
                        uploadController.dropUrls(drop.urls, treeDelegate.handle,
                                                  treeDelegate.isRoot);
                    } else {
                        root.dragProxy.sourceNav.moveHandlesTo(root.dragProxy.handles,
                                                               treeDelegate.handle,
                                                               treeDelegate.isRoot);
                    }
                }
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
