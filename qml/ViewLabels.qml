pragma Singleton
import QtQuick
import MegaExplorer

// What a location is called on screen. C++ supplies a name only when the location
// has one of its own -- a folder's, later an album's -- and leaves it empty for
// every screen the app itself invents, the Cloud Drive root included. Those labels
// belong here rather than in src/core, which links no Qt and so could never wrap
// them in qsTr().
//
// Every call site that shows a location's name (the breadcrumb, the tab strip)
// routes through label(), so a new special view is one line here rather than one
// branch per site. glyph() answers the same question for the icon in front of it.
QtObject {
    function label(kind, isRoot, name) {
        if (kind === ViewKind.Favourites)
            return qsTr("Favourites");
        if (kind === ViewKind.Recents)
            return qsTr("Recent");
        // Only the bin's own top is named here; a folder inside it keeps its own
        // name, which is why this is gated on isRoot and Favourites is not.
        if (kind === ViewKind.Rubbish && isRoot)
            return qsTr("Rubbish bin");
        if (isRoot)
            return qsTr("Cloud Drive");
        return name;
    }

    // Empty for an ordinary folder: those are named, not iconified, in the
    // breadcrumb. Callers key the icon's visibility off that.
    function glyph(kind, isRoot) {
        if (kind === ViewKind.Favourites)
            return Theme.glyph.favouriteOutline;
        if (kind === ViewKind.Recents)
            return Theme.glyph.recent;
        if (kind === ViewKind.Rubbish && isRoot)
            return Theme.glyph.menu.moveToRubbish;
        if (isRoot)
            return Theme.glyph.cloud;
        return "";
    }
}
