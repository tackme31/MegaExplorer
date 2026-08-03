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
    // Main.qml's window-wide DragProxy. Drop target only, same as
    // FolderTreePanel.qml -- a pin is a shortcut, not something to drag out of.
    required property var dragProxy

    // Passed in rather than read off root.height: this ColumnLayout's own
    // height is derived from its children, so capping a child against it would
    // be a binding loop. SidePanel supplies the panel's own (SplitView-driven)
    // height instead.
    required property real availableHeight

    spacing: 0

    // Hidden entirely, header included, while nothing is pinned -- an empty
    // labeled box above the tree would just be dead space.
    visible: quickAccessModel.count > 0

    FolderPinMenu {
        id: pinMenu
    }

    // Windows 11 renders a section header as slightly smaller and weakly
    // coloured, not bold (3-6). De-emphasis by colour rather than opacity, so
    // it doesn't go muddy against surfaceAlt.
    Label {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.spacing.lg
        Layout.topMargin: Theme.spacing.sm
        Layout.bottomMargin: Theme.spacing.sm
        text: qsTr("Quick access")
        font.pixelSize: Theme.font.caption
        color: Theme.color.textSecondary
        elide: Text.ElideRight
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

        model: quickAccessModel

        delegate: ItemDelegate {
            id: pinDelegate

            required property string name
            required property var handle

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

            // Same per-delegate arrangement as FolderTreePanel.qml's, including
            // its internal/external branching -- see that file for why the
            // hover handlers key off dragProxy.active while onDropped keys off
            // the event's own payload. A pin whose target was deleted on
            // another device simply never accepts: both canDropHandlesOn and
            // canUploadTo bottom out in a kENoEnt for a handle that no longer
            // resolves.
            DropArea {
                id: dropArea
                anchors.fill: parent
                // "text/uri-list" is what an external OS drop matches on --
                // without it those drops are silently ignored here.
                keys: ["application/x-megaexplorer-nodes", "text/uri-list"]

                property bool accepting: false

                // Payload read off root.dragProxy rather than the event's own
                // drag.source, same reasoning as FolderTreePanel.qml's.
                onEntered: drag => {
                    if (root.dragProxy.active) {
                        dropArea.accepting = root.dragProxy.sourceNav.canDropHandlesOn(
                                    root.dragProxy.handles, pinDelegate.handle, false);
                    } else if (drag.hasUrls) {
                        dropArea.accepting = uploadController.canUploadTo(pinDelegate.handle,
                                                                          false);
                        // Only the external branch touches drag.accepted: the
                        // move path relies on implicit acceptance by key match.
                        drag.accepted = dropArea.accepting;
                    } else {
                        dropArea.accepting = false;
                    }
                }
                onExited: dropArea.accepting = false
                onDropped: drop => {
                    if (dropArea.accepting) {
                        if (drop.hasUrls) {
                            drop.accept(Qt.CopyAction);
                            uploadController.dropUrls(drop.urls, pinDelegate.handle, false);
                        } else {
                            root.dragProxy.sourceNav.moveHandlesTo(root.dragProxy.handles,
                                                                   pinDelegate.handle, false);
                        }
                    }
                    dropArea.accepting = false;
                }
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
        }
    }
}
