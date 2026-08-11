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
// branch per site.
QtObject {
    function label(kind, isRoot, name) {
        if (kind === ViewKind.Favourites)
            return qsTr("Favourites");
        if (isRoot)
            return qsTr("Cloud Drive");
        return name;
    }
}
