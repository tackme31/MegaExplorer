import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts

// Explorer 11's address-bar row (extracted from Main.qml in R6-3): back/up/
// refresh, the breadcrumb, the search field and the overflow menu. Loaded by
// Main.qml's header: slot only while signed in -- the caption row above it is
// deliberately outside that gate, so it stays in Main.qml.
//
// Address bar/breadcrumb only. Phase 9 wrapped this ToolBar in a ColumnLayout to
// stack a TabStrip above it; Phase 17b moved that strip onto the caption row
// itself (see CaptionBar.qml), leaving the wrapper with a single child and no
// reason to exist.
ToolBar {
    id: root

    // Main.qml's single window-wide DragProxy, relayed to the breadcrumb's
    // per-segment drop targets. Handed down rather than reached by id because a
    // separately loaded file can't see Main.qml's ids -- same route
    // CaptionBar/SidePanel/TabContentPane take.
    required property var dragProxy

    // The three dialogs live in Main.qml and are only ever open()ed from here,
    // so this relays instead of taking three object injections -- Main.qml owns
    // them, so opening one is its call, not ours (AboutDialog.qml makes the same
    // argument for its own licensesRequested).
    signal aboutRequested
    signal licensesRequested
    signal signOutRequested

    // Explorer 11's address bar row, measured (S8b). Taller than the caption row
    // above it, which stays at 40 -- they share a surface, not a height. The
    // default would be whatever the tallest child asks for.
    implicitHeight: Theme.rowHeight.toolbar

    // A ToolBar is a Pane, so the RowLayout declared below is not the
    // contentItem -- it's a child of the implicit one the Pane creates, and that
    // item is sized to contentWidth/contentHeight, which default to the
    // *implicit* size of what's inside rather than the space available. Left
    // alone, that meant the row got 34px (its tallest child, the SearchField)
    // inside FluentWinUI3's 8px top padding: 8..42 in a 40px band, i.e. the
    // search field spilling 2px past the row's own bottom stroke, and the
    // breadcrumb -- the only child with Layout.fillHeight -- centring 4px above
    // the 32px buttons beside it (S8a).
    //
    // So the band is stated three times over, each one closing a different gap:
    // padding puts the content rect where we want it, contentWidth/contentHeight
    // give the implicit item the whole rect, and the RowLayout's anchors.fill
    // takes all of it. Dropping Fluent's SafeArea.margins term is deliberate --
    // it is 0 on desktop Windows.
    topPadding: 0
    bottomPadding: 0
    leftPadding: Theme.spacing.md
    rightPadding: Theme.spacing.md
    contentWidth: availableWidth
    contentHeight: availableHeight

    // The active tab's rounded background ends flush against the top of this row
    // and has to continue into it, so the two must be the same surface. Fluent's
    // default ToolBar background happens to match in dark, but is the panel
    // shade in light, which would show the seam.
    background: Rectangle {
        color: Theme.color.surface

        // Carries the whole boundary on its own in light, where this row and the
        // content below it are both `surface`.
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.border.thin
            color: Theme.color.stroke
        }
    }

    // Fills the Pane's implicit content item, which the block above has already
    // reduced to exactly the band minus its padding -- so unlike before S8a
    // there are no margins to restate here.
    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacing.md

        ToolbarIconButton {
            text: Theme.glyph.back
            ToolTip.text: qsTr("Back")
            // tabsController.currentNavigation is only null during the brief
            // login/logout state transition (see AuthController.authState's
            // Connections in Main.qml) -- ?./?? guard against that window.
            enabled: tabsController.currentNavigation?.canGoBack ?? false
            onClicked: tabsController.currentNavigation?.goBack()
        }

        // Goes to the current folder's parent, and -- unlike a "forward" button
        // -- pushes that onto the back stack like any other navigation, so
        // "back" undoes it (S7-a).
        ToolbarIconButton {
            text: Theme.glyph.up
            ToolTip.text: qsTr("Up")
            enabled: tabsController.currentNavigation?.canGoUp ?? false
            onClicked: tabsController.currentNavigation?.goUp()
        }

        // No enabled binding, unlike the two above: there's no state where
        // refreshing is meaningless to the user, and refresh() is itself a no-op
        // before the first load.
        ToolbarIconButton {
            text: Theme.glyph.refresh
            ToolTip.text: qsTr("Refresh")
            onClicked: tabsController.currentNavigation?.refresh()
        }

        // 700:300 against the search field below. Qt Quick Layouts distributes
        // space between fillWidth items in the ratio of their preferred sizes
        // ("If there are multiple items with fillWidth set to true, the layout
        // will grow or shrink the items relative to the ratio of their preferred
        // size" -- Qt 6.11 Layout docs), so the literals below are that ratio,
        // not pixel values. They were a bare 7 and 3 until S7 gave the search
        // field a minimumWidth: a preferred size below an item's own minimum is
        // silently raised to it, which turned the ratio into 7:160 and left the
        // breadcrumb three characters wide. Both numbers therefore have to stay
        // clear of that minimum. minimumWidth: 0 is spelled out (it's already
        // the default for a non-layout item) because "no minimum width" is a
        // deliberate requirement here.
        Breadcrumb {
            navController: tabsController.currentNavigation
            dragProxy: root.dragProxy
            model: tabsController.currentNavigation?.breadcrumb ?? []
            Layout.fillWidth: true
            Layout.preferredWidth: 700
            Layout.minimumWidth: 0
            // Not fillHeight: since S8b the trail is drawn inside a framed box,
            // and the frame has to be the same height as the search field beside
            // it rather than the whole band. Bound to that field's actual height
            // instead of a token because it's the style that decides it (34, see
            // below).
            Layout.preferredHeight: searchField.height
            Layout.alignment: Qt.AlignVCenter
        }

        // Qt 6.10's own search control, so the magnifier and the clear button
        // come from the style rather than from us (S7-b). It has no
        // placeholderText, which is the price of that.
        SearchField {
            id: searchField

            Layout.fillWidth: true
            Layout.preferredWidth: 300
            // Unlike the breadcrumb beside it, this must not collapse to
            // nothing: below roughly this the two indicators leave no room to
            // type in.
            Layout.minimumWidth: 160
            // Centred at its own 34px rather than stretched or pinned to the
            // buttons' 32: that height is max(background 32, contentHeight + 10,
            // searchIndicator 24 + 10) in FluentWinUI3, and forcing it down
            // would squeeze the style's own 5px vertical padding and its two
            // indicators.
            Layout.alignment: Qt.AlignVCenter
            // MegaApi::search() blocks the GUI thread synchronously, so search
            // on Enter only rather than on every keystroke -- live: true would
            // freeze the UI once per keystroke.
            live: false
            // Enter (searchTriggered) and the magnifier (searchButtonPressed)
            // are separate signals; only the first is gated by live.
            onSearchTriggered: tabsController.currentNavigation?.search(text)
            onSearchButtonPressed: tabsController.currentNavigation?.search(text)
            // The style draws this button but doesn't empty the field, so
            // clearing the text is ours to do as well.
            onClearButtonPressed: {
                text = "";
                tabsController.currentNavigation?.search("");
            }

            // Navigating drops the query C++-side; this field owns its text, so
            // nothing else can empty it. Assigning text does not re-trigger a
            // search: the field is live: false and only its two signals call one.
            // Same for the filter popup's selections, which C++ drops in the same
            // breath and cannot reach either.
            Connections {
                target: tabsController.currentNavigation ?? null

                function onSearchCleared() {
                    searchField.text = "";
                    filterPopup.reset();
                }
            }
        }

        // Hidden in the favourites listing: the search box there narrows favourites
        // through IMegaClient::listFavourites, which takes no filter, so the popup
        // would set state that nothing reads (FolderNavigationController::
        // runVisibleSearch).
        ToolbarIconButton {
            id: filterButton

            visible: tabsController.currentNavigation?.viewKind !== ViewKind.Favourites
            text: Theme.glyph.filter
            ToolTip.text: qsTr("Search filters")
            onClicked: filterPopup.opened ? filterPopup.close() : filterPopup.open()

            // The chip: a filter that is set but whose popup is shut is otherwise
            // invisible, and a search returning three rows out of a thousand with no
            // sign of why is the failure mode this whole control has to avoid.
            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: Theme.spacing.sm
                width: Theme.spacing.md
                height: width
                radius: width / 2
                color: Theme.color.accent
                visible: filterPopup.filterActive
            }

            SearchFilterPopup {
                id: filterPopup

                navController: tabsController.currentNavigation ?? null
                // Right-aligned under the button, for the same reason the "More"
                // menu below is: this sits at the right edge of the window.
                x: filterButton.width - width
                y: filterButton.height
            }
        }

        ToolbarIconButton {
            id: moreButton

            text: Theme.glyph.more
            ToolTip.text: qsTr("More")
            onClicked: moreMenu.open()

            Menu {
                id: moreMenu

                // Right-aligned under the button rather than popup()'s default
                // of "top-left corner at the cursor": this button sits at the
                // right edge of the window, so the default put all 280px of the
                // menu outside it.
                //
                // Bound rather than passed to popup(x, y) only because it reads
                // better -- the two behave identically, and neither lands in
                // time for the *first* open. See the known issue in Phase 17b's
                // docs/PROGRESS.md log: these bindings are first evaluated ~72ms
                // after onOpened, so the first menu of a session is drawn
                // overlapping the toolbar for a frame before dropping into
                // place.
                x: moreButton.width - width
                y: moreButton.height

                // Insurance, not decoration: since Qt 6.8 a Menu may resolve to
                // Popup.Native, and the docs are explicit that the delegate is
                // then not used for rendering -- which would silently drop the
                // account header below entirely. Windows is a native-menu
                // platform.
                popupType: Popup.Window

                // Explicit, because a Menu's contentItem is a ListView and does
                // not aggregate its children's implicitWidth -- the account
                // header's own 280 is ignored and the menu would otherwise stay
                // at the ~200 the three text items need, which is too narrow for
                // an email address.
                width: 280

                // No open animation, deliberately. Any enter transition flickers
                // here, whatever property it drives, because this popup is a
                // window of its own (popupType above): the OS maps it and the
                // render thread can present one frame of the *un-animated* state
                // before the animation's first tick lands. Measured: the
                // transition's `from` value is applied 1ms after visible becomes
                // true, but the next animated value is 26-71ms behind it -- one
                // to three frames in which the menu shows itself, snaps to
                // `from`, and only then animates in.
                //
                // The style's own transition drives __heightScale from 0.33, so
                // the symptom was a menu that appeared at full height,
                // collapsed, and grew back; swapping it for an opacity fade only
                // changed it to appear, vanish, and fade back. Both read as "the
                // menu closed and reopened". A menu of one-row entries is small
                // and fast enough not to show it; a 280x279 one with a header
                // does.
                enter: null

                // The style's popup background is a nine-patch PNG whose
                // interior is not a flat colour but WinUI's acrylic *grain* --
                // per-pixel noise, a couple of levels either side of #353535.
                // Its middle section is only 102x90, and BorderImage stretches
                // that to fill the item, so at this menu's size it is a ~2.7x
                // bilinear magnification and the grain smears into visible
                // marbling. Tiling repeats it at 1:1, which is how the texture
                // was meant to be seen. Ordinary menus are small enough not to
                // show it.
                //
                // The BorderImage is found by duck-typing rather than by index:
                // the background item also carries the style's own
                // high-contrast rectangle as a child.
                Component.onCompleted: {
                    for (const child of background.children) {
                        if (child.horizontalTileMode !== undefined) {
                            child.horizontalTileMode = BorderImage.Repeat;
                            child.verticalTileMode = BorderImage.Repeat;
                        }
                    }
                }

                // The one and only trigger for loading account data. Nothing is
                // fetched at login, so a user who never opens this menu costs no
                // requests at all. Cheap to call on every open: the profile half
                // runs once per session and the storage half skips a read that
                // is already in flight.
                onAboutToShow: accountController.refresh()

                AccountMenuHeader {}
                MenuSeparator {}

                // IconMenuItem, not MenuItem, for the leading glyph -- same rows
                // as a context menu gets, so the two kinds of menu don't
                // disagree about whether items have icons.
                IconMenuItem {
                    text: qsTr("About MegaExplorer")
                    glyph: Theme.glyph.menu.about
                    onTriggered: root.aboutRequested()
                }
                IconMenuItem {
                    text: qsTr("Open source licenses")
                    glyph: Theme.glyph.menu.licenses
                    onTriggered: root.licensesRequested()
                }
                MenuSeparator {}
                IconMenuItem {
                    text: qsTr("Sign out")
                    glyph: Theme.glyph.menu.signOut
                    onTriggered: root.signOutRequested()
                }
            }
        }
    }

    // Toolbar-row button: a square, glyph-only ToolButton with a tooltip
    // standing in for the label it no longer has (S7). Declared inline rather
    // than as its own file -- same call as CaptionBar.qml's CaptionButton.
    //
    // Nothing here may reference `root`: an inline component does not share the
    // scope of the file declaring it, so a root.* binding would be a
    // ReferenceError the moment this type is used from another file.
    component ToolbarIconButton: ToolButton {
        // Without this, clicking one of these while the grid is showing (view
        // mode doesn't change, so no StackLayout focus handoff fires) leaves
        // focus on the button and arrow keys dead until the view is re-clicked.
        focusPolicy: Qt.NoFocus
        implicitWidth: 32
        implicitHeight: 32
        // Stated here rather than at each use site: these are 32px inside a 40px
        // row, and a RowLayout's cross-axis placement for an item that can't
        // fill the row is an engine default nobody should be relying on (S8a).
        // Harmless if the button is ever used outside a layout.
        Layout.alignment: Qt.AlignVCenter
        font.family: Theme.font.iconFamily
        ToolTip.delay: 500
        ToolTip.visible: hovered
    }
}
