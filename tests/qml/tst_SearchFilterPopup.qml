import QtQuick
import QtTest
import MegaExplorer

// The advanced-search facets. Since Apply and Clear became the only ways a selection
// reaches C++, this file is the net under a failure mode a screenshot cannot see: a
// facet that pushes early re-runs a blocking search on every pick, and one that never
// pushes leaves the funnel button doing nothing at all. Both look identical in a
// still image of the popup.
//
// The controller arrives as a required property, which is what makes this testable
// -- the runner has no engine hook, so main.cpp's context properties do not exist
// here (see QmlTestMain.cpp).
TestCase {
    id: testCase
    name: "SearchFilterPopup"

    Component {
        id: popupComponent
        SearchFilterPopup {}
    }

    // QtObject rather than a JS literal: the popup calls this through an optional
    // call on a var property, and a plain literal would swallow the call instead of
    // recording it.
    Component {
        id: navComponent

        QtObject {
            id: nav

            property int viewKind: ViewKind.CloudDrive
            property int calls: 0
            property int lastType: -1
            property int lastCategory: -1
            property int lastTime: -1
            property bool lastFavourite: false
            property bool lastThisFolder: false

            // FolderNavigationController's filter properties, which the popup now
            // reads back instead of mirroring. Written here only by setSearchFilter, as
            // C++ writes them only in the same call.
            property int searchFilterNodeType: SearchNodeType.Any
            property int searchFilterCategory: SearchCategory.Any
            property int searchFilterCreatedWithin: SearchTimeWindow.Any
            property bool searchFilterFavouritesOnly: false
            property bool searchFilterThisFolderOnly: false

            // Navigation drops the filter C++-side (searchCleared) without the popup
            // pushing anything; this is that half, for the tests that need it.
            function dropFilter() {
                nav.searchFilterNodeType = SearchNodeType.Any;
                nav.searchFilterCategory = SearchCategory.Any;
                nav.searchFilterCreatedWithin = SearchTimeWindow.Any;
                nav.searchFilterFavouritesOnly = false;
                nav.searchFilterThisFolderOnly = false;
            }

            function setSearchFilter(nodeType, category, createdWithin, favouritesOnly, thisFolderOnly) {
                calls += 1;
                lastType = nodeType;
                lastCategory = category;
                lastTime = createdWithin;
                lastFavourite = favouritesOnly;
                lastThisFolder = thisFolderOnly;
                nav.searchFilterNodeType = nodeType;
                nav.searchFilterCategory = category;
                nav.searchFilterCreatedWithin = createdWithin;
                nav.searchFilterFavouritesOnly = favouritesOnly;
                nav.searchFilterThisFolderOnly = thisFolderOnly;
            }
        }
    }

    function makePopup() {
        const nav = createTemporaryObject(navComponent, testCase);
        const popup = createTemporaryObject(popupComponent, testCase, {
                                                "navController": nav
                                            });
        verify(popup);
        return {
            "popup": popup,
            "nav": nav
        };
    }

    // A user's pick is a currentIndex write *plus* the activated/toggled signal, and
    // both halves matter here: an eagerly-pushing handler can only be re-added to
    // those signals, so a test that assigned the property alone would go on passing
    // through exactly the regression it exists to catch.
    function pick(popup, type, category, time, favourite) {
        popup.typeSelector.currentIndex = type;
        popup.typeSelector.activated(type);
        popup.categorySelector.currentIndex = category;
        popup.categorySelector.activated(category);
        popup.timeSelector.currentIndex = time;
        popup.timeSelector.activated(time);
        popup.favouriteSelector.checked = favourite;
        popup.favouriteSelector.toggled();
    }

    function test_pickingAFacetDoesNotSearch() {
        const f = makePopup();

        pick(f.popup, SearchNodeType.Files, SearchCategory.Photo, SearchTimeWindow.PastWeek, true);

        compare(f.nav.calls, 0);
        verify(f.popup.dirty);
        verify(f.popup.pendingActive);
        // The chip on the funnel button reads this: a filter nobody applied yet must
        // not claim the results are narrowed.
        verify(!f.popup.filterActive);
    }

    function test_applyPushesEveryFacetAtOnce() {
        const f = makePopup();

        pick(f.popup, SearchNodeType.Files, SearchCategory.Audio, SearchTimeWindow.PastDay, true);
        f.popup.apply();

        compare(f.nav.calls, 1);
        compare(f.nav.lastType, SearchNodeType.Files);
        compare(f.nav.lastCategory, SearchCategory.Audio);
        compare(f.nav.lastTime, SearchTimeWindow.PastDay);
        compare(f.nav.lastFavourite, true);
        verify(f.popup.filterActive);
        verify(!f.popup.dirty);
    }

    function test_reopeningDiscardsAnUnappliedEdit() {
        const f = makePopup();

        pick(f.popup, SearchNodeType.Files, SearchCategory.Any, SearchTimeWindow.Any, false);
        f.popup.apply();

        // Edited, then dismissed by anything other than Apply.
        pick(f.popup, SearchNodeType.Folders, SearchCategory.Any, SearchTimeWindow.PastYear, true);
        f.popup.open();
        tryCompare(f.popup, "opened", true);

        compare(f.popup.typeSelector.currentIndex, SearchNodeType.Files);
        compare(f.popup.timeSelector.currentIndex, SearchTimeWindow.Any);
        compare(f.popup.favouriteSelector.checked, false);
        verify(!f.popup.dirty);
        compare(f.nav.calls, 1);
    }

    function test_clearDropsTheAppliedFilterWithoutASecondApply() {
        const f = makePopup();

        pick(f.popup, SearchNodeType.Files, SearchCategory.Audio, SearchTimeWindow.Any, false);
        f.popup.apply();
        f.popup.open();
        tryCompare(f.popup, "opened", true);
        f.popup.clearFilter();

        // Unlike Apply, Clear is a "start over" step and leaves the popup up.
        compare(f.popup.opened, true);
        compare(f.nav.calls, 2);
        compare(f.nav.lastType, SearchNodeType.Any);
        compare(f.nav.lastCategory, SearchCategory.Any);
        compare(f.nav.lastTime, SearchTimeWindow.Any);
        compare(f.nav.lastFavourite, false);
        verify(!f.popup.filterActive);
        verify(!f.popup.pendingActive);
        verify(!f.popup.dirty);
    }

    // The other half of the same button: with nothing applied the cleared filter is
    // the one C++ already holds, and pushing it re-runs a search for no change.
    function test_clearWithNothingAppliedPushesNothing() {
        const f = makePopup();

        pick(f.popup, SearchNodeType.Files, SearchCategory.Audio, SearchTimeWindow.PastDay, true);
        f.popup.clearFilter();

        compare(f.nav.calls, 0);
        verify(!f.popup.pendingActive);
        verify(!f.popup.filterActive);
    }

    // The funnel button stays visible while currentNavigation is null (the
    // login/logout transition), so Apply is reachable with nothing to push to.
    function test_applyWithNoControllerClaimsNothing() {
        const popup = createTemporaryObject(popupComponent, testCase, {
                                                "navController": null
                                            });
        verify(popup);

        pick(popup, SearchNodeType.Files, SearchCategory.Audio, SearchTimeWindow.PastDay, true);
        popup.apply();

        verify(!popup.filterActive);
    }

    // Clear reaches C++ too, so it needs the same net as Apply: clearing the controls
    // while the mirror stands would disable Clear over a filter the chip still shows.
    function test_clearWithNoControllerChangesNothing() {
        const popup = createTemporaryObject(popupComponent, testCase, {
                                                "navController": null
                                            });
        verify(popup);

        pick(popup, SearchNodeType.Files, SearchCategory.Audio, SearchTimeWindow.PastDay, true);
        popup.clearFilter();

        verify(popup.pendingActive);
        compare(popup.typeSelector.currentIndex, SearchNodeType.Files);
    }

    function test_pickingFoldersDropsTheCategory() {
        const f = makePopup();

        f.popup.categorySelector.currentIndex = SearchCategory.Photo;
        f.popup.typeSelector.currentIndex = SearchNodeType.Folders;
        f.popup.typeSelector.activated(SearchNodeType.Folders);

        compare(f.popup.categorySelector.currentIndex, SearchCategory.Any);
        compare(f.nav.calls, 0);
    }

    function test_revertPendingFollowsCppDroppingTheFilter() {
        const f = makePopup();

        pick(f.popup, SearchNodeType.Files, SearchCategory.Video, SearchTimeWindow.PastMonth, true);
        f.popup.apply();
        f.nav.dropFilter();
        f.popup.revertPending();

        // C++ has already dropped it (searchCleared), so pushing here would re-run a
        // search the navigation just cancelled.
        compare(f.nav.calls, 1);
        verify(!f.popup.filterActive);
        verify(!f.popup.pendingActive);
        compare(f.popup.typeSelector.currentIndex, SearchNodeType.Any);
        compare(f.popup.favouriteSelector.checked, false);
    }

    // The regression this popup's applied* side was rebuilt for: the filter belongs to
    // a tab, the popup is one control for the whole window, and a mirror kept here left
    // the chip lit over whichever tab last applied one.
    function test_switchingControllerShowsThatTabsFilter() {
        const f = makePopup();
        const other = createTemporaryObject(navComponent, testCase);

        pick(f.popup, SearchNodeType.Files, SearchCategory.Video, SearchTimeWindow.PastMonth, true);
        f.popup.apply();
        verify(f.popup.filterActive);

        // Switching tabs, which is the only thing that moves navController.
        f.popup.navController = other;

        verify(!f.popup.filterActive);
        compare(other.calls, 0);

        // And the pending edit follows on the next open, so Apply cannot carry the
        // previous tab's selection into this one.
        f.popup.open();
        tryCompare(f.popup, "opened", true);
        compare(f.popup.typeSelector.currentIndex, SearchNodeType.Any);
        verify(!f.popup.dirty);

        f.popup.navController = f.nav;
        verify(f.popup.filterActive);
    }

    // The two listings ignore one facet each C++-side, and a control that pushes a
    // value nothing reads is what this popup was hidden from those views to avoid.
    function test_facetsTheQueryListingsIgnoreAreDisabled_data() {
        return [
                    {
                        tag: "cloud drive",
                        kind: ViewKind.CloudDrive,
                        type: true,
                        favourite: true,
                        thisFolder: true
                    },
                    {
                        tag: "favourites",
                        kind: ViewKind.Favourites,
                        type: true,
                        favourite: false,
                        thisFolder: false
                    },
                    {
                        tag: "recents",
                        kind: ViewKind.Recents,
                        type: false,
                        favourite: true,
                        thisFolder: false
                    }
                ];
    }

    function test_facetsTheQueryListingsIgnoreAreDisabled(data) {
        const f = makePopup();
        f.nav.viewKind = data.kind;

        compare(f.popup.typeSelector.enabled, data.type);
        compare(f.popup.favouriteSelector.enabled, data.favourite);
        // Both listings are rooted at the Cloud Drive root, so there is no open folder
        // to scope to and C++ ignores the facet there.
        compare(f.popup.thisFolderSelector.enabled, data.thisFolder);
        // Never gated on the view: both listings honour it.
        verify(f.popup.timeSelector.enabled);
    }

    // The scope checkbox is the one facet that changes which SDK call runs rather than
    // narrowing the result, so it has to travel the same Apply/Clear path as the rest.
    function test_theFolderScopeTravelsWithTheOtherFacets() {
        const f = makePopup();

        f.popup.thisFolderSelector.checked = true;
        f.popup.thisFolderSelector.toggled();
        verify(f.popup.dirty);
        verify(f.popup.pendingActive);
        verify(!f.popup.filterActive);

        f.popup.apply();

        compare(f.nav.calls, 1);
        compare(f.nav.lastThisFolder, true);
        verify(f.popup.filterActive);
        verify(!f.popup.dirty);

        f.popup.open();
        tryCompare(f.popup, "opened", true);
        f.popup.clearFilter();

        compare(f.nav.calls, 2);
        compare(f.nav.lastThisFolder, false);
        compare(f.popup.thisFolderSelector.checked, false);
        verify(!f.popup.filterActive);
    }

    // The pending edit is still one per window (only applied* follows the tab), so a
    // Folders picked in a Cloud Drive tab survives into a Recents tab until the next
    // open. Greying the category off that stale value would leave both pickers dead
    // with only Clear as a way out.
    function test_recentsKeepsTheCategoryPickerWhenTheStaleTypeIsFolders() {
        const f = makePopup();

        f.popup.typeSelector.currentIndex = SearchNodeType.Folders;
        f.nav.viewKind = ViewKind.Recents;

        verify(!f.popup.typeSelector.enabled);
        verify(f.popup.categorySelector.enabled);
    }
}
