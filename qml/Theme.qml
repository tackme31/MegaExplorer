pragma Singleton

import QtQuick

// Design tokens, defined in one place (docs/DESIGN_IMPROVEMENT.md 0-1). S1
// defined them; S2 onwards replaces the views' inline literals with them
// section by section, so some tokens still have no consumer yet.
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

    // Tree-row geometry, shared so the pin rows can derive the same numbers
    // instead of restating them as literals (3-4: a hand-tuned leftPadding: 20
    // was already 8px out of step). The values themselves are Basic's own
    // TreeViewDelegate defaults; only indent departs from it, dropping the
    // stock 20 -- which is just indicator.width reused -- to 16 (3-9).
    readonly property QtObject tree: QtObject {
        id: treeGeom
        readonly property int margin: 4
        readonly property int indicatorWidth: 20 // D1b: the hit area never shrinks
        readonly property int indent: 16
        readonly property int spacing: 4
        // Row left edge to leading icon, at depth 0. Both halves of the side
        // panel line up on this.
        readonly property int contentIndent: treeGeom.margin + treeGeom.indicatorWidth
                                             + treeGeom.spacing
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

    // Segoe Fluent Icons code points, spelled as escapes: the raw glyphs sit in
    // the private use area, where an editor or a grep shows nothing at all.
    // Every one of these also exists at the same code point in Windows 10's
    // Segoe MDL2 Assets (checked against both fonts' cmap), so no fallback path
    // is needed -- the same conclusion CaptionBar.qml records for the window
    // buttons. Later phases add their glyphs here (S6 sort arrows, S7
    // breadcrumb chevron and nav buttons, S9 view-mode toggles).
    // The folder is E8D5 FolderFill, a solid shape -- closest to Explorer's own
    // filled yellow folder, which is a raster asset out of imageres.dll rather
    // than a glyph. file stays an outline; the font has no filled page that
    // reads at 16px.
    readonly property QtObject glyph: QtObject {
        readonly property string folder: "\uE8D5" // FolderFill
        readonly property string file: "\uE7C3"   // Page
        // Two glyphs rather than one rotated 90 degrees (what Basic's
        // TreeViewDelegate does with its arrow PNG): the font already draws
        // both, and a rotation would need a transformOrigin to stay centred.
        readonly property string chevronRight: "\uE76C" // ChevronRight
        readonly property string chevronDown: "\uE70D"  // ChevronDown
        // Explorer's quick-access rows carry this at their right edge. The
        // diagonal outline (Pin) rather than E840 Pinned, which is upright and
        // filled -- Explorer draws the diagonal one.
        readonly property string pin: "\uE718" // Pin
    }

    readonly property QtObject color: QtObject {
        // D3: Explorer 11 ordering -- the panel side is the lighter surface.
        readonly property color surface: root.isLight ? "#ffffff" : "#202020"
        readonly property color surfaceAlt: root.isLight ? "#f3f3f3" : "#272727"
        readonly property color stroke: root.isLight ? "#e5e5e5" : "#303030"

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

        // The folder is the one coloured thing in an otherwise monochrome icon
        // set, which is what makes it readable at 16px (D3 = Explorer 11).
        // Files stay textSecondary so the two never compete.
        readonly property color accentFolder: root.isLight ? "#ffb900" : "#ffd166"

        // Copied verbatim from FluentWinUI3/impl/ButtonBackground.qml's `subtle`
        // branch, so a control we background ourselves keeps hover/press
        // feedback identical to the untouched Fluent controls beside it.
        readonly property color subtleHover: root.isLight ? Qt.rgba(0, 0, 0, 0.04) : Qt.rgba(1, 1, 1,
                                                                                             0.06)
        readonly property color subtlePressed: root.isLight ? Qt.rgba(0, 0, 0, 0.02) : Qt.rgba(1, 1,
                                                                                               1, 0.04)

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
