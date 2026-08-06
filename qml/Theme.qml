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

    // Item opacity, not colour, unlike the de-emphasized text colours above:
    // this dims an icon or a thumbnail as much as a label.
    readonly property QtObject opacity: QtObject {
        // Rows sitting on the clipboard as a cut -- Explorer ghosts them until
        // the paste actually moves them.
        readonly property real cut: 0.5
    }

    readonly property QtObject rowHeight: QtObject {
        readonly property int compact: 28 // tree + pin rows (D1a)
        readonly property int normal: 32  // detail view rows and header (S6)
        readonly property int caption: 40 // caption row / tab strip (S2)
        // Address bar row. Taller than the caption above it -- both are the
        // same surface, but Explorer 11 measures 48 here and 40 there, and the
        // breadcrumb frame added in S8b needs the extra room to sit inside.
        readonly property int toolbar: 48
        // Status bar. Explorer 11 measures 24-28 here; matching `compact`
        // above is a coincidence, not a shared decision -- S8b had to split
        // one shared token back into caption/toolbar for exactly that reason.
        readonly property int status: 28
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
        // Grid tiles (S8), drawn in place of a thumbnail. Sized against the
        // 80px thumbnail slot below rather than as a step in a scale -- S1's
        // guessed 32 left the slot looking mostly empty.
        readonly property int lg: 48
    }

    // Account section of the "More" menu. Not folded into iconSize.lg even
    // though both happen to be 48 today: that one is an icon scale and this is
    // a portrait, and S8b is the recorded case of one token serving two
    // decisions and later having to be split apart again.
    readonly property QtObject avatar: QtObject {
        readonly property int size: 48
        readonly property int initialFontSize: 20
    }

    // Storage-usage bar in that same section. A pair of plain Rectangles
    // rather than a ProgressBar, so the track can be drawn with no fill at all
    // while the figures are still loading -- a zero-width fill would read as
    // "nothing used", which is a different claim from "not known yet".
    readonly property QtObject storageBar: QtObject {
        readonly property int height: 6
    }

    // Thumbnail-grid tile geometry (S8). The tile is inset inside its cell by
    // gap/2 on every side, so neighbouring tiles end up a full gap apart and
    // the view's own top/bottom margin only has to supply the other half.
    readonly property QtObject grid: QtObject {
        readonly property int gap: 8
        readonly property int tileWidth: 112
        // md 8 + thumb 80 + sm 4 + label 38 + md 8. Spelled out rather than
        // summed from the other tokens so the tile can't silently change
        // height when one of them is retuned for an unrelated consumer.
        readonly property int tileHeight: 138
        readonly property int thumbSize: 80
        readonly property int labelHeight: 38 // two lines at font.body
    }

    // Segoe Fluent Icons code points, spelled as escapes: the raw glyphs sit in
    // the private use area, where an editor or a grep shows nothing at all.
    // Every one of these also exists at the same code point in Windows 10's
    // Segoe MDL2 Assets (checked against both fonts' cmap), so no fallback path
    // is needed -- the same conclusion CaptionBar.qml records for the window
    // buttons. Later phases add their glyphs here, re-running that cmap check
    // first (docs/DESIGN_IMPROVEMENT.md section 11 has the script).
    // The folder is E8D5 FolderFill, a solid shape -- closest to Explorer's own
    // filled yellow folder, which is a raster asset out of imageres.dll rather
    // than a glyph. file stays an outline; the font has no filled page that
    // reads at 16px.
    readonly property QtObject glyph: QtObject {
        id: glyphSet
        readonly property string folder: "\uE8D5" // FolderFill
        readonly property string file: "\uE7C3"   // Page
        // Two glyphs rather than one rotated 90 degrees (what Basic's
        // TreeViewDelegate does with its arrow PNG): the font already draws
        // both, and a rotation would need a transformOrigin to stay centred.
        readonly property string chevronRight: "\uE76C" // ChevronRight
        readonly property string chevronDown: "\uE70D"  // ChevronDown
        // Ascending sort in the detail view's header (S6); descending reuses
        // chevronDown above.
        readonly property string chevronUp: "\uE70E" // ChevronUp
        // Explorer's quick-access rows carry this at their right edge. The
        // diagonal outline (Pin) rather than E840 Pinned, which is upright and
        // filled -- Explorer draws the diagonal one.
        readonly property string pin: "\uE718" // Pin
        // Toolbar row (S7). more replaces the typed-out identical-to sign the
        // overflow button used to draw; cloud sits inside the breadcrumb's root
        // segment so it disappears with it when the trail overflows.
        readonly property string back: "\uE72B"  // Back
        readonly property string up: "\uE74A"    // Up
        readonly property string more: "\uE712"  // More
        readonly property string cloud: "\uE753" // Cloud
        // The toolbar's refresh button (added after S7); same cmap check
        // re-run for it, present in both fonts.
        readonly property string refresh: "\uE72C" // Refresh
        // Status bar's view-mode toggles (S9), replacing the typed-out box
        // and hamburger characters that were the last non-ASCII glyphs left
        // in the QML. Not E80A Tiles, the obvious partner to List: that one
        // carries COLR colour layers, which Qt honours, so it painted as a
        // full-colour icon in the middle of an otherwise monochrome bar.
        // Presence in the cmap is necessary but not sufficient -- check how
        // a new glyph actually paints.
        readonly property string viewList: "\uE8FD" // List
        readonly property string viewGrid: "\uE8A9" // ViewAll
        // Toast dismiss (S10). No cmap check needed: TabStrip's tab-close and
        // CaptionBar's window-close already paint this one.
        readonly property string close: "\uE8BB" // ChromeClose

        // Menu-row leading icons. One entry per context-menu action ID (the
        // same IDs ActionCatalog.qml keys on) plus the three "More" menu rows,
        // so picking an icon is an edit here and nowhere else.
        //
        // PROVISIONAL: chosen to be plausible, not vetted. Unlike every glyph
        // above, these have been through neither the Segoe MDL2 cmap check nor
        // the "check how it actually paints" one that E80A Tiles failed (see
        // viewList above) -- do both when the final set is settled on.
        readonly property QtObject menu: QtObject {
            readonly property string newFolder: "\uE8F4"     // NewFolder
            readonly property string download: "\uE896"      // Download
            readonly property string openInNewTab: "\uE8A7"  // OpenInNewWindow
            readonly property string pin: glyphSet.pin
            readonly property string unpin: "\uE77A"         // Unpin
            readonly property string cut: "\uE8C6"           // Cut
            readonly property string copy: "\uE8C8"          // Copy
            readonly property string paste: "\uE77F"         // Paste
            readonly property string rename: "\uE8AC"        // Rename
            readonly property string moveToRubbish: "\uE74D" // Delete
            readonly property string selectAll: "\uE8B3"     // SelectAll
            readonly property string refresh: glyphSet.refresh
            readonly property string about: "\uE946"         // Info
            readonly property string licenses: "\uE9a4"      // TextBulletListSquare
            readonly property string signOut: "\uE7E8"       // Leave
        }
    }

    readonly property QtObject color: QtObject {
        // D3: Explorer 11 ordering -- the panel side is the lighter surface.
        readonly property color surface: root.isLight ? "#ffffff" : "#202020"
        readonly property color surfaceAlt: root.isLight ? "#f3f3f3" : "#272727"
        readonly property color stroke: root.isLight ? "#e5e5e5" : "#303030"

        // Fill of a FluentWinUI3 input field. The style paints no colour for it
        // -- the background is a 9-patch sprite (light|dark/images/
        // combobox-background.png) of plain white at alpha 178/255 light,
        // 15/255 dark, i.e. WinUI's ControlFillColorDefault. Restated as an
        // alpha so anything we frame ourselves composites to the same shade the
        // sprite does over the same surface (S8b: the breadcrumb frame sits
        // beside a SearchField and the two were visibly different).
        readonly property color fieldFill: Qt.rgba(1, 1, 1, root.isLight ? 178 / 255 : 15 / 255)

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
        // Rubber-band rectangle (Phase 21). Much fainter than `selection`
        // above: the band is drawn over rows that already carry that fill, and
        // two comparable alphas stacked read as a third, darker state.
        readonly property color band: Qt.rgba(root.sysPalette.highlight.r, root.sysPalette.highlight.g,
                                              root.sysPalette.highlight.b, 0.15)
        readonly property color danger: root.isLight ? "#c42b1c" : "#ff99a4"

        // Groove of the storage bar. Deliberately not `stroke`: that one means
        // "border", and reusing it here would tie a fill to a line weight (the
        // S8b lesson again). Sits between surfaceAlt and stroke in contrast so
        // an unfilled track still reads as a track on either surface.
        readonly property color storageTrack: root.isLight ? Qt.rgba(0, 0, 0, 0.09) : Qt.rgba(1, 1,
                                                                                              1, 0.11)

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
        // Beyond this the oldest card is dropped rather than queued: a stack
        // tall enough to reach the toolbar stops being a notification.
        readonly property int maxVisible: 3
    }

    // Transition durations. Distinct from toast.dismissMs above, which is a
    // wait, not a movement.
    readonly property QtObject motion: QtObject {
        readonly property int fast: 150
    }
}
