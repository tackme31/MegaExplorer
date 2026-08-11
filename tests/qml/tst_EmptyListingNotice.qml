import QtQuick
import QtTest
import MegaExplorer

// The notice is invisible almost all of the time, so every failure mode here is
// silent: a wrong condition either hides it on the one screen that needs it, or
// leaves it printed over a listing that has rows. A screenshot only catches the
// second.
//
// The controller arrives as a required property, which is what makes this
// testable at all -- the runner has no engine hook, so main.cpp's context
// properties do not exist here (see QmlTestMain.cpp).
TestCase {
    id: testCase
    name: "EmptyListingNotice"

    // TestCase declares itself visible: false, and Item.visible reads back the
    // *effective* value -- so without this every assertion below would pass on
    // its own emptiness (same reason as tst_PreviewPane.qml).
    visible: true

    Component {
        id: noticeComponent
        EmptyListingNotice {}
    }

    // QtObject rather than a JS literal, so the property bindings inside the
    // notice actually re-evaluate when a test changes one.
    Component {
        id: navComponent

        QtObject {
            id: fakeNav

            property int viewKind: ViewKind.Favourites
            property bool searchActive: false
            property QtObject fileListModel: QtObject {
                property int count: 0
            }
        }
    }

    function makeNotice(props) {
        const nav = createTemporaryObject(navComponent, testCase, props);
        return {
            "nav": nav,
            "notice": createTemporaryObject(noticeComponent, testCase, {
                                                "navController": nav
                                            })
        };
    }

    function test_showsOnAnEmptyFavouritesListing() {
        const built = makeNotice({});
        verify(built.notice.visible);
    }

    function test_hidesAsSoonAsTheListingHasRows() {
        const built = makeNotice({});
        built.nav.fileListModel.count = 1;
        verify(!built.notice.visible);
    }

    function test_staysAwayFromOrdinaryFolders() {
        // An empty folder is a normal thing to be looking at; v1 speaks only for
        // the favourites listing.
        const built = makeNotice({
                                     "viewKind": ViewKind.CloudDrive
                                 });
        verify(!built.notice.visible);
    }

    function test_searchingSwapsTheWordingAndDropsTheHint() {
        const built = makeNotice({});
        const unsearched = built.notice.children[0].text;
        const hintVisible = built.notice.children[1].visible;

        built.nav.searchActive = true;

        verify(hintVisible);
        verify(built.notice.children[0].text !== unsearched);
        // The "right-click to add one" hint answers a question the user did not
        // ask once they are filtering.
        verify(!built.notice.children[1].visible);
    }
}
