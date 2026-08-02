pragma Singleton

import QtQuick

// Design tokens, defined in one place (docs/DESIGN_IMPROVEMENT.md 0-1). This
// phase (S1) only defines them -- nothing consumes them yet; the existing views
// keep their inline literals until S2 onwards replaces them section by section.
//
// Structure follows Qt's own FluentWinUI3/Config.qml: pragma Singleton over a
// QtObject, grouped by nested read-only QtObjects, light/dark picked per token
// rather than by swapping whole palettes.
QtObject {
    id: root

    // Same expression FluentWinUI3 uses throughout its own files, so this can
    // never disagree with the style about which scheme is active.
    readonly property bool isLight: Application.styleHints.colorScheme === Qt.Light

    // A singleton is not an Item, so it has no attached palette. One instance
    // here replaces the seven SystemPalette objects currently spread across the
    // views (those get removed as each file starts consuming these tokens).
    readonly property SystemPalette sysPalette: SystemPalette {}

    readonly property QtObject spacing: QtObject {
        readonly property int xs: 2
        readonly property int sm: 4
        readonly property int md: 8
        readonly property int lg: 12
        readonly property int xl: 16
    }

    readonly property QtObject radius: QtObject {
        readonly property int sm: 4
        readonly property int md: 8
    }

    readonly property QtObject border: QtObject {
        readonly property int thin: 1
        // Drop-target outline; currently written as a literal 2 in seven places.
        readonly property int drop: 2
    }

    readonly property QtObject rowHeight: QtObject {
        readonly property int compact: 28 // tree + pin rows (D1a)
        readonly property int normal: 32  // detail view rows and header (S6)
    }

    readonly property QtObject font: QtObject {
        readonly property int body: 14 // FluentWinUI3's own default size
        readonly property int caption: 12
        readonly property string iconFamily: "Segoe Fluent Icons"
    }

    readonly property QtObject iconSize: QtObject {
        readonly property int sm: 16 // row leading icons (S4/S5)
        readonly property int lg: 32 // grid tiles (S8)
    }

    readonly property QtObject color: QtObject {
        // D3: Explorer 11 ordering -- the panel side is the lighter surface.
        readonly property color surface: root.isLight ? "#ffffff" : "#202020"
        readonly property color surfaceAlt: root.isLight ? "#f3f3f3" : "#272727"
        readonly property color stroke: root.isLight ? "#e5e5e5" : "#303030"
        // Provisional -- D3 fixed `stroke` only. Confirm on screen during S3.
        readonly property color strokeSubtle: root.isLight ? "#ebebeb" : "#2d2d2d"

        readonly property color text: root.sysPalette.text
        // De-emphasis by colour, not item opacity, so it stays clean when the
        // surface underneath changes (3-6). Ratios are Fluent's text-secondary.
        readonly property color textSecondary: root.isLight ? Qt.rgba(0, 0, 0, 0.61) : Qt.rgba(1, 1,
                                                                                               1, 0.79)

        readonly property color accent: root.sysPalette.highlight
        readonly property color onAccent: root.sysPalette.highlightedText
        readonly property color selection: Qt.rgba(root.sysPalette.highlight.r,
                                                   root.sysPalette.highlight.g,
                                                   root.sysPalette.highlight.b, 0.35)
        readonly property color dragGhost: Qt.rgba(root.sysPalette.highlight.r,
                                                   root.sysPalette.highlight.g,
                                                   root.sysPalette.highlight.b, 0.85)
        readonly property color danger: root.isLight ? "#c42b1c" : "#ff99a4"

        // Windows-mandated close-button colours. Kept out of the theme-following
        // group deliberately: these must not track light/dark.
        readonly property color closeHover: "#e81123"
        readonly property color closePressed: "#c42b1c"
        readonly property color closeGlyphOn: "white"
    }

    readonly property QtObject toast: QtObject {
        readonly property int maxWidth: 320
        readonly property int margin: 16
        readonly property int dismissMs: 6000
    }
}
