import QtQuick
// Before any other QtQuick.Controls import, same rule as its siblings.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// The side panel's fixed entry points: screens the app invents, as opposed to
// folders it found. Albums land here as a further row, which is why this is a
// section rather than rows inlined into SidePanel.qml.
ColumnLayout {
    id: root

    required property var navController

    spacing: 0

    // Inline rather than a file of its own: every metric below is shared with
    // the pin rows and nothing outside this section instantiates it. What each
    // row supplies is only its kind, glyph and the two entry points -- the
    // screens differ by controller call, not by appearance.
    component SpecialViewRow: ItemDelegate {
        id: viewRow

        required property int kind
        required property string glyph
        // Stated per row rather than derived here: the two screens differ in what
        // "on it" means. Favourites cannot be navigated into, so its kind alone
        // settles it; the bin can, and a folder inside it must not light this row
        // (the tree marks that instead).
        required property bool current
        // Called on click (this tab) and middle-click (background tab).
        required property var openHere
        required property var openInNewTab
        // null for a row with nothing to offer. Stated per row rather than derived
        // from kind, like current above: what a row's menu holds is the row's own
        // business, and the Favourites screen has no bin-wide action of its own.
        required property var openContextMenu

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

        // Through ViewLabels rather than a second qsTr() each: the tab strip and
        // the breadcrumb already name these screens from there. isRoot is true
        // because these rows name a screen's own top, which is what the Rubbish
        // branch there keys on.
        text: ViewLabels.label(viewRow.kind, true, "")

        contentItem: RowLayout {
            spacing: Theme.spacing.md

            // Fixed square box, not a bare glyph: these glyphs' advance widths
            // differ from the folder glyph's, and without it the label would
            // start at a different x than the pin rows below (FileIcon.qml).
            Label {
                Layout.preferredWidth: Theme.iconSize.sm
                Layout.alignment: Qt.AlignVCenter
                horizontalAlignment: Text.AlignHCenter
                font.family: Theme.font.iconFamily
                font.pixelSize: Theme.iconSize.sm
                color: Theme.color.textSecondary
                text: viewRow.glyph
            }

            Label {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                text: viewRow.text
                elide: Text.ElideRight
                // Stated outright rather than inherited, same as the pins' (D1a).
                font.pixelSize: Theme.font.body
                color: Theme.color.text
            }
        }

        background: Rectangle {
            radius: Theme.radius.sm
            color: viewRow.current ? Theme.color.selection : (viewRow.hovered ? Theme.color.subtleHover :
                                                                                "transparent")
        }

        // No NodeDropArea, deliberately: these rows are not drop targets
        // (24b 2.1). With no DropArea at all there is nothing to refuse.

        onClicked: viewRow.openHere()

        // AbstractButton takes LeftButton itself, so this never competes with
        // onClicked -- the arrangement FolderTreePanel.qml already relies on.
        TapHandler {
            acceptedButtons: Qt.MiddleButton
            onTapped: viewRow.openInNewTab()
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: {
                if (viewRow.openContextMenu)
                    viewRow.openContextMenu();
            }
        }
    }

    // Above Favourites, with the bin left last: the bin is where every file manager
    // puts it, and the two query screens read as a pair above it.
    SpecialViewRow {
        kind: ViewKind.Recents
        // No atRoot term, for the Favourites row's reason below.
        current: root.navController ? root.navController.viewKind === ViewKind.Recents : false
        glyph: Theme.glyph.recent
        openHere: () => root.navController?.openRecents()
        openInNewTab: () => tabsController.addRecentsTab()
        openContextMenu: null
    }

    SpecialViewRow {
        kind: ViewKind.Favourites
        // No atRoot term: the favourites screen cannot be navigated into, and its
        // synthesized breadcrumb segment reports isRoot false anyway.
        current: root.navController ? root.navController.viewKind === ViewKind.Favourites : false
        // Outline EB51, not the solid EB52 of the file rows' marker -- this row
        // names a place, not a node's state (24a).
        glyph: Theme.glyph.favouriteOutline
        openHere: () => root.navController?.openFavourites()
        openInNewTab: () => tabsController.addFavouritesTab()
        openContextMenu: null
    }

    SpecialViewRow {
        kind: ViewKind.Rubbish
        current: root.navController ? (root.navController.viewKind === ViewKind.Rubbish
                                       && root.navController.atRoot) : false
        glyph: Theme.glyph.menu.moveToRubbish
        openHere: () => root.navController?.openRubbish()
        openInNewTab: () => tabsController.addRubbishTab()
        openContextMenu: () => rubbishRowMenu.popup()
    }

    // Asked for with ViewKind.Rubbish, which is what keeps this menu to the bin's
    // own actions: the tree rows and the pins ask the same site with CloudDrive
    // (FolderPinMenu.qml), so the two lists never overlap.
    ActionMenu {
        id: rubbishRowMenu

        actionIds: MenuActions.forSite(MenuActions.FolderRow, ViewKind.Rubbish)

        // No handle/name/entries: nothing this menu offers addresses a node. It is
        // assigned rather than bound for the same reason every other site does it --
        // a menu must not change target while it is open.
        onAboutToShow: rubbishRowMenu.context = {
            "requestEmptyRubbish": () => confirmEmptyRubbishDialog.open()
        }
    }

    // Reaches the current tab's controller rather than one of its own: emptying the
    // bin is app-wide, so any tab's controller issues the same single request.
    ConfirmEmptyRubbishDialog {
        id: confirmEmptyRubbishDialog
        mutController: tabsController.currentMutations
    }
}
