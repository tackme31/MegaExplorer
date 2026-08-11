import QtQuick
// Before any other QtQuick.Controls import, same rule as its siblings.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// The side panel's fixed entry points: screens the app invents, as opposed to
// folders it found. One row for now; Rubbish and Albums land here as further
// children, which is why this is a section rather than a row inlined into
// SidePanel.qml.
ColumnLayout {
    id: root

    required property var navController

    spacing: 0

    ItemDelegate {
        id: favouritesRow

        Layout.fillWidth: true

        // Every metric here is the pin rows', so the whole panel keeps one row
        // rhythm: the height token, the indent that lines the leading glyph up
        // with the tree's depth-0 rows, and the pill's insets. topPadding and
        // bottomPadding are stated because FluentWinUI3's ItemDelegate carries
        // 8, which in a 28px row pushes a 16px icon 2px low (S8a).
        implicitHeight: Theme.rowHeight.compact
        leftPadding: Theme.tree.contentIndent
        rightPadding: Theme.spacing.sm + Theme.spacing.md
        topPadding: 0
        bottomPadding: 0
        leftInset: Theme.spacing.sm
        rightInset: Theme.spacing.sm

        // Without this, clicking the row strands keyboard focus in the panel and
        // deadens the file view's arrow keys until it is re-clicked.
        focusPolicy: Qt.NoFocus

        // Through ViewLabels rather than a second qsTr("Favourites"): the tab
        // strip and the breadcrumb already name this screen from there.
        text: ViewLabels.label(ViewKind.Favourites, false, "")

        // Which screen the tab is on, not which handle it holds -- this one has
        // none, which is also why no pin and no tree row lights up beside it.
        readonly property bool isCurrent: root.navController ? root.navController.viewKind
                                                               === ViewKind.Favourites : false

        contentItem: RowLayout {
            spacing: Theme.spacing.md

            // Fixed square box, not a bare glyph: the heart's advance width
            // differs from the folder glyph's, and without it the label would
            // start at a different x than the pin rows below (FileIcon.qml).
            // Outline EB51, not the solid EB52 of the file rows' marker -- this
            // row names a place, not a node's state (24a).
            Label {
                Layout.preferredWidth: Theme.iconSize.sm
                Layout.alignment: Qt.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                font.family: Theme.font.iconFamily
                font.pixelSize: Theme.iconSize.sm
                color: Theme.color.textSecondary
                text: Theme.glyph.favouriteOutline
            }

            Label {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: favouritesRow.text
                elide: Text.ElideRight
                // Stated outright rather than inherited, same as the pins' (D1a).
                font.pixelSize: Theme.font.body
                color: Theme.color.text
            }
        }

        background: Rectangle {
            radius: Theme.radius.sm
            color: favouritesRow.isCurrent ? Theme.color.selection : (favouritesRow.hovered
                                                                      ? Theme.color.subtleHover :
                                                                        "transparent")
        }

        // No NodeDropArea, deliberately: this row is not a drop target (24b 2.1).
        // With no DropArea at all there is nothing to refuse.

        onClicked: root.navController?.openFavourites()

        // AbstractButton takes LeftButton itself, so this never competes with
        // onClicked -- the arrangement FolderTreePanel.qml already relies on.
        TapHandler {
            acceptedButtons: Qt.MiddleButton
            onTapped: tabsController.addFavouritesTab()
        }
    }
}
