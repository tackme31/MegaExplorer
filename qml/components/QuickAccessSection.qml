import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml/Breadcrumb.qml/FileTableView.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// Pinned-folder list (Phase 11), sitting above the folder tree inside
// SidePanel.qml -- Explorer's own placement. Flat by design: a pin is a
// shortcut to one folder, never expandable, so this is a ListView and not a
// second root inside FolderTreeModel's tree.
//
// model is quickAccessModel, an app-lifetime context property shared across
// every tab (main.cpp). navController is the *active* tab's
// FolderNavigationController, rebound by Main.qml on each tab switch -- same
// arrangement as FolderTreePanel.qml, and the isCurrent formula below is
// deliberately identical to that file's so both halves of the panel highlight
// the current folder the same way.
ColumnLayout {
    id: root

    required property var navController
    // Main.qml's window-wide DragProxy. A pin is still not something to drag
    // *out of* the panel, so this is a drop target like FolderTreePanel.qml --
    // but the reorder gesture (Phase 22a) borrows the proxy's ghost, in its
    // ghost-only mode, so the dragged pin's name follows the cursor.
    required property var dragProxy

    // Passed in rather than read off root.height: this ColumnLayout's own
    // height is derived from its children, so capping a child against it would
    // be a binding loop. SidePanel supplies the panel's own (SplitView-driven)
    // height instead.
    required property real availableHeight

    spacing: 0

    // Hidden entirely while nothing is pinned -- an empty box above the tree
    // would just be dead space. The gap that separates this section from the
    // window edge above it belongs to SidePanel.qml, not here.
    visible: quickAccessModel.count > 0

    FolderPinMenu {
        id: pinMenu
    }

    ListView {
        id: pinList

        Layout.fillWidth: true
        // Sized to its content, but never allowed to push the folder tree out
        // of the panel when a lot of folders are pinned.
        Layout.preferredHeight: contentHeight
        Layout.maximumHeight: root.availableHeight * 0.4

        clip: true
        // Only scrollable once it's actually been capped, so a short list
        // doesn't swallow wheel events meant for the tree below.
        interactive: contentHeight > height
        // Frees the left button for the delegates' reorder DragHandler, the way
        // FolderTreePanel.qml already does it: without this the Flickable's own
        // click-drag panning steals the gesture. Wheel scrolling is unaffected.
        acceptedButtons: Qt.NoButton

        model: quickAccessModel

        // Reorder state (Phase 22a) lives on the view, not the dragged
        // delegate: the insertion line below reads it, and a delegate can be
        // recycled mid-drag once auto-scrolling moves it out of view.
        property int reorderFrom: -1
        property int reorderInsert: -1
        property var reorderHandle: undefined
        readonly property real rowH: Theme.rowHeight.compact

        // Arithmetic, not itemAt(): an auto-scrolling list outruns delegate
        // realization, the same reason Phase 21's band selector converts a
        // rectangle to rows by hand. Rounding (rather than flooring) puts the
        // boundary at each row's midpoint, which is what makes the insertion
        // line land "between" rows. Result is 0..count, an insertion point.
        function insertIndexAt(viewY) {
            const row = Math.round((viewY + pinList.contentY) / pinList.rowH);
            return Math.max(0, Math.min(row, pinList.count));
        }

        function beginReorder(row, handle) {
            pinList.reorderFrom = row;
            pinList.reorderInsert = row;
            pinList.reorderHandle = handle;
        }

        function endReorder() {
            pinList.reorderFrom = -1;
            pinList.reorderInsert = -1;
            pinList.reorderHandle = undefined;
            autoScroller.release();
            root.dragProxy.finishGhost();
        }

        function commitReorder() {
            if (pinList.reorderFrom < 0)
                return;
            const from = pinList.reorderFrom;
            const handle = pinList.reorderHandle;
            // Insertion point -> final row: pulling the dragged pin out first
            // shifts everything below it up by one.
            const to = pinList.reorderInsert > from ? pinList.reorderInsert - 1 :
                                                      pinList.reorderInsert;


            pinList.endReorder();
            if (to !== from)
                quickAccessModel.move(handle, to);
        }

        delegate: ItemDelegate {
            id: pinDelegate

            required property string name
            required property var handle
            required property int index

            width: pinList.width
            // Same token FolderTreePanel.qml's delegate uses, so both halves of
            // the panel share one row rhythm (D1a).
            implicitHeight: Theme.rowHeight.compact
            // Lines the leading icons up with the tree's depth-0 rows below.
            // Derived, not hand-matched: the tree's offset is the sum of three
            // style defaults, so the old literal 20 was quietly 8px out and
            // could not have been kept in step by hand (3-4).
            leftPadding: Theme.tree.contentIndent
            // Inset (which moves the pill only) plus a gutter inside it, so the
            // trailing pin glyph stops 8px short of the pill's own right edge
            // instead of touching the panel border (S8a).
            rightPadding: Theme.spacing.sm + Theme.spacing.md

            // FluentWinUI3's ItemDelegate config carries topPadding/
            // bottomPadding 8, which in the 28px row this delegate declares
            // above leaves a 12px content box at y=8 -- too short for a 16px
            // icon, so the whole row rendered 2px low (S8a). Any row whose
            // height we set ourselves has to state its vertical padding too.
            topPadding: 0
            bottomPadding: 0

            // The rounded pill of Windows 11's navigation pane, same as the
            // tree rows (3-1).
            leftInset: Theme.spacing.sm
            rightInset: Theme.spacing.sm

            // Matches TabStrip.qml's TabButton and FolderTreePanel.qml's
            // delegate: without this, clicking a row strands keyboard focus
            // here instead of the file view, deadening its arrow-key
            // navigation until the view is re-clicked.
            focusPolicy: Qt.NoFocus

            text: pinDelegate.name

            // Spelled out only to get the leading icon in; the label half
            // restates what the style's own contentItem already did. Kept the
            // same shape as FolderTreePanel.qml's so the two halves of the
            // panel gain the icon on identical terms (S4).
            contentItem: RowLayout {
                spacing: Theme.spacing.md

                // All three children state their own vertical centring rather
                // than leaning on the layout engine's cross-axis default, the
                // same way FileTableView.qml's row does (S8a).
                FileIcon {
                    Layout.alignment: Qt.AlignVCenter
                    isFolder: true
                }

                Label {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    text: pinDelegate.text
                    elide: Text.ElideRight
                    // Stated outright rather than inherited, same as the tree's
                    // label (D1a).
                    font.pixelSize: Theme.font.body
                    color: Theme.color.text
                }

                // Explorer's trailing pin marker -- what tells a pinned
                // shortcut apart from an ordinary tree row. Indicator only: the
                // row is one click target, and unpinning stays in the
                // right-click menu.
                Label {
                    Layout.preferredWidth: Theme.iconSize.sm
                    Layout.alignment: Qt.AlignVCenter
                    horizontalAlignment: Text.AlignHCenter
                    font.family: Theme.font.iconFamily
                    font.pixelSize: Theme.iconSize.sm
                    color: Theme.color.textSecondary
                    text: Theme.glyph.pin
                }
            }

            readonly property bool isCurrent: root.navController ? (!root.navController.atRoot
                                                                    && pinDelegate.handle
                                                                    === root.navController.currentHandle) :
                                                                   false

            background: Rectangle {
                radius: Theme.radius.sm
                color: pinDelegate.isCurrent ? Theme.color.selection : (pinDelegate.hovered
                                                                        ? Theme.color.subtleHover :
                                                                          "transparent")
                border.width: dropArea.accepting ? Theme.border.drop : 0
                border.color: Theme.color.accent
            }

            // Shared drop behaviour lives in NodeDropArea.qml. A pin whose
            // target was deleted on another device simply never accepts: both
            // canDropHandlesOn and canUploadTo bottom out in a kENoEnt for a
            // handle that no longer resolves. A pin is never the account root,
            // hence the literal false.
            NodeDropArea {
                id: dropArea
                anchors.fill: parent
                dragProxy: root.dragProxy
                uploads: uploadController
                targetHandle: pinDelegate.handle
                targetIsRoot: false
                targetKind: ViewKind.CloudDrive
            }

            // activate() rather than navigateTo() directly: the pin's target
            // may have been deleted on another device since login, so the model
            // verifies it first and answers with either activated() or
            // missing() (both handled in Main.qml, which is what knows about
            // tabs and dialogs).
            onClicked: quickAccessModel.activate(pinDelegate.handle, false)

            // AbstractButton only accepts LeftButton itself, so these two never
            // compete with onClicked above -- the same arrangement
            // FolderTreePanel.qml's TreeViewDelegate already relies on.
            TapHandler {
                acceptedButtons: Qt.MiddleButton
                onTapped: quickAccessModel.activate(pinDelegate.handle, true)
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: pinMenu.popupFor(pinDelegate.handle, false, pinDelegate.name)
            }

            // Reorder (Phase 22a). Deliberately *not* a Qt drag: the gesture
            // never leaves pinList, so starting one would only mean teaching
            // the five node-move DropAreas to ignore a new Drag.key. Same
            // signal set as FileGridView.qml's move drag, minus the payload;
            // taking the exclusive grab is also what cancels the delegate's own
            // pending click, so a reorder never navigates.
            DragHandler {
                id: reorderHandler
                target: null
                acceptedButtons: Qt.LeftButton
                // Note what is *not* here: `xAxis.enabled: false`. It reads
                // like the right constraint for a one-column list, but it also
                // gates activation -- a sideways drag then never takes the
                // exclusive grab, so the ghost doesn't appear until the pointer
                // has travelled the drag threshold vertically as well, which
                // feels like the gesture is broken. Both axes stay live and the
                // logic below simply ignores x.

                onActiveChanged: {
                    if (!reorderHandler.active) {
                        pinList.commitReorder();
                        return;
                    }
                    pinList.beginReorder(pinDelegate.index, pinDelegate.handle);
                    root.dragProxy.beginGhost(pinDelegate.name,
                                              reorderHandler.centroid.scenePosition);
                }

                onActiveTranslationChanged: {
                    if (pinList.reorderFrom < 0)
                        return;
                    const scenePos = reorderHandler.centroid.scenePosition;
                    root.dragProxy.moveTo(scenePos);
                    const viewY = pinList.mapFromItem(null, scenePos).y;
                    pinList.reorderInsert = pinList.insertIndexAt(viewY);
                    autoScroller.track(viewY);
                }

                onCanceled: pinList.endReorder()
            }
        }

        // The list is capped at 40% of the panel, so a long pin list really can
        // need scrolling mid-drag. Writes contentY directly, so it works even
        // while `interactive` is false.
        DragAutoScroller {
            id: autoScroller
            flickable: pinList
        }

        // parent: pinList rather than the default -- an Item declared inside a
        // Flickable lands in its contentItem and would scroll away with the
        // rows (the trap BandSelector.qml documents). Inset to match the rows'
        // pill so the line spans exactly the row, not the panel.
        Rectangle {
            id: insertLine

            parent: pinList
            z: 2
            visible: pinList.reorderFrom >= 0
            x: Theme.spacing.sm
            width: pinList.width - 2 * Theme.spacing.sm
            height: Theme.border.drop
            color: Theme.color.accent

            // Clamped into the viewport, not just computed: the "after the last
            // pin" insertion point lands exactly on the bottom edge, where
            // pinList's own clip would swallow the line entirely.
            y: Math.min(pinList.reorderInsert * pinList.rowH - pinList.contentY, pinList.height
                        - insertLine.height)
        }
    }
}
