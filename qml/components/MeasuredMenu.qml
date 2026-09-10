import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml/Breadcrumb.qml/FileTableView.qml.
import QtQuick.Controls.FluentWinUI3

// A Menu as wide as its widest row. ActionMenu.qml is one, and so is every
// submenu it builds, which is why this is its own file.
//
// FluentWinUI3's Menu gives its ListView contentItem an implicitHeight but no
// implicitWidth (Qt 6.11), so Menu's own `implicitContentWidth + padding` term
// is always 0 and every menu sat at the style's 200px background width. Labels
// longer than "Move to Rubbish bin" were silently elided -- both toggle
// actions' longer branch ("Unpin from Quick access", "Remove from Favourites")
// among them.
Menu {
    id: root

    // Painted by the row that opens this menu as a submenu (ActionMenu's
    // delegate); unused on a top-level menu.
    property string glyph: ""

    // Measured, not bound: itemAt() is not a notifying property, so a binding
    // over it would never re-evaluate. Re-run on every open below.
    property real measuredContentWidth: 0

    implicitWidth: Math.max(root.implicitBackgroundWidth + root.leftInset + root.rightInset,
                            root.measuredContentWidth + root.leftPadding + root.rightPadding)

    // implicitContentWidth + the item's own padding, deliberately not the
    // item's implicitWidth: MenuItem floors that at its 200px background, which
    // would widen every menu by the Menu's own padding instead of only the ones
    // that actually overflow.
    function remeasure() {
        let widest = 0;
        for (let i = 0; i < root.count; ++i) {
            const item = root.itemAt(i);
            if (item)
                widest = Math.max(widest, item.implicitContentWidth + item.leftPadding
                                  + item.rightPadding);
        }
        root.measuredContentWidth = widest;
    }

    // Connections rather than a declarative onAboutToShow: two of ActionMenu's
    // three sites declare one of their own, and a handler in a derived
    // component replaces the base component's outright.
    Connections {
        target: root
        function onAboutToShow() {
            root.remeasure();
        }
    }
}
