import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml/ActionMenu.qml.
import QtQuick.Controls.FluentWinUI3

// A MenuItem with a Segoe Fluent Icons glyph in a leading gutter. Used by
// every menu in the app -- ActionMenu.qml's generated rows and Main.qml's
// "More" menu -- so the gutter width and the glyph's colour are decided once.
//
// The style's own icon slot (MenuItem's icon.name/icon.source, drawn by its
// IconLabel contentItem) is deliberately unused: it takes an image URL, and
// every icon in this app is a font glyph instead (Theme.glyph). Rather than
// replace the contentItem -- which would mean reimplementing the style's
// mirroring, spacing and submenu-arrow padding -- this draws the glyph itself
// and buys the room for it through implicitTextPadding, the property the style
// already uses to shift a checkable item's label clear of its checkmark.
MenuItem {
    id: root

    // Empty means no icon, not no gutter: rows without one still line their
    // text up with the rows that have one.
    property string glyph: ""

    // textPadding itself is read-only -- Menu sets it to the widest
    // implicitTextPadding among its items, which is what keeps the "More"
    // menu's hand-written rows and a generated menu's rows in one column.
    implicitTextPadding: Theme.iconSize.sm + root.spacing

    Text {
        // Same placement expressions the style uses for MenuItem's arrow and
        // indicator, so the gutter tracks any padding the style retunes.
        x: root.mirrored ? root.width - width - root.rightPadding : root.leftPadding
        y: root.topPadding + (root.availableHeight - height) / 2
        width: Theme.iconSize.sm
        height: Theme.iconSize.sm
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter

        font.family: Theme.font.iconFamily
        font.pixelSize: Theme.iconSize.sm
        // The control's palette, not Theme.color.text: this resolves to the
        // disabled group on its own, so a greyed row's glyph greys with its
        // label instead of staying at full strength.
        color: root.palette.text
        text: root.glyph
    }
}
