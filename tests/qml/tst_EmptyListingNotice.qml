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

    function test_showsOnAnEmptyOrdinaryFolderToo() {
        const built = makeNotice({
                                     "viewKind": ViewKind.CloudDrive
                                 });
        verify(built.notice.visible);
    }

    function test_wordsAnEmptyFolderDifferentlyFromAnEmptyFavourites() {
        const favourites = makeNotice({});
        const folder = makeNotice({
                                      "viewKind": ViewKind.CloudDrive
                                  });

        // Both lines: a folder's hint points at "New folder", the favourites
        // one at "Add to Favourites", so sharing either wording would send the
        // user to the wrong menu row.
        verify(favourites.notice.children[0].text !== folder.notice.children[0].text);
        verify(favourites.notice.children[1].text !== folder.notice.children[1].text);
    }

    function test_searchingSwapsTheWordingAndDropsTheHint_data() {
        return [
                    {
                        "tag": "favourites",
                        "kind": ViewKind.Favourites
                    },
                    {
                        "tag": "folder",
                        "kind": ViewKind.CloudDrive
                    }
                ];
    }

    function test_searchingSwapsTheWordingAndDropsTheHint(data) {
        const built = makeNotice({
                                     "viewKind": data.kind
                                 });
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
