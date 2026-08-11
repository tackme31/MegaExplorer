import QtQuick
import QtTest
import MegaExplorer

// ViewLabels is a pragma Singleton holding one pure function, so the test needs
// no fixture at all. What it pins down is the split the function exists to keep:
// a location with a name of its own shows that name, and every screen the app
// invents gets its label here rather than from C++.
TestCase {
    name: "ViewLabels"

    function test_rootIsLabelledHereNotByCpp() {
        // C++ leaves the root segment nameless, so an empty name is not a bug to
        // paper over -- it is the signal that this function owns the label.
        compare(ViewLabels.label(ViewKind.CloudDrive, true, ""), qsTr("Cloud Drive"));
    }

    function test_favouritesGetTheirOwnLabel() {
        compare(ViewLabels.label(ViewKind.Favourites, false, ""), qsTr("Favourites"));
    }

    function test_aRealFolderKeepsItsOwnName() {
        compare(ViewLabels.label(ViewKind.CloudDrive, false, "photos"), "photos");
    }
}
