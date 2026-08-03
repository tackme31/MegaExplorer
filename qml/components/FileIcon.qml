import QtQuick
import QtQuick.Controls.FluentWinUI3

// The one place that decides what a folder and a file look like, so the list,
// the grid, the tree, the pins and the tabs cannot drift apart (S4). D6 stops
// this phase at two kinds; the extension-to-type mapping that replaces the
// glyph choice lands here and nowhere else.
//
// An Item wrapping the Label rather than a Label itself: Text-derived types
// declare implicitWidth/implicitHeight read-only, and a square box is the
// point -- the two glyphs have different advance widths, and without it the
// name beside the icon starts at a different x for a folder than for a file.
Item {
    id: root

    required property bool isFolder
    property int size: Theme.iconSize.sm

    implicitWidth: root.size
    implicitHeight: root.size

    Label {
        anchors.fill: parent
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter

        font.family: Theme.font.iconFamily
        font.pixelSize: root.size
        color: root.isFolder ? Theme.color.accentFolder : Theme.color.textSecondary
        text: root.isFolder ? Theme.glyph.folder : Theme.glyph.file
    }
}
