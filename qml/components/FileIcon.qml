import QtQuick
import QtQuick.Controls.FluentWinUI3

// The one place that decides what a folder and a file look like, so the list,
// the grid, the tree, the pins and the tabs cannot drift apart (S4). D6 stops
// this phase at two kinds; which glyph a file gets is FileTypeIcons.qml's
// whitelist, and a caller that leaves fileName empty (the tree, the pins, the
// tabs -- all folders) gets the fallback it never draws.
//
// An Item wrapping the Label rather than a Label itself: Text-derived types
// declare implicitWidth/implicitHeight read-only, and a square box is the
// point -- the two glyphs have different advance widths, and without it the
// name beside the icon starts at a different x for a folder than for a file.
Item {
    id: root

    required property bool isFolder
    property string fileName: ""
    property int size: Theme.iconSize.sm

    // Not gated on isFolder, even though a folder never draws it: the Label's
    // ternaries below depend on isFolder too, and on a view's reused delegate
    // flipping folder -> file they can re-evaluate before this binding does.
    // A null here was then read for .family/.glyph (evolve/095).
    readonly property var typeIcon: FileTypeIcons.forFileName(root.fileName)

    implicitWidth: root.size
    implicitHeight: root.size

    Label {
        anchors.fill: parent
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter

        font.family: root.isFolder ? Theme.font.iconFamily : root.typeIcon.family
        font.pixelSize: root.size
        color: root.isFolder ? Theme.color.accentFolder : Theme.color.textSecondary
        text: root.isFolder ? Theme.glyph.folder : root.typeIcon.glyph
    }
}
