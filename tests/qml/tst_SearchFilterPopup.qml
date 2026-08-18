import QtQuick
import QtTest
import MegaExplorer

// The advanced-search facets. Since Apply became the only way a selection reaches
// C++, this file is the net under a failure mode a screenshot cannot see: a facet
// that pushes early re-runs a blocking search on every pick, and one that never
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
            property int calls: 0
            property int lastType: -1
            property int lastCategory: -1
            property int lastTime: -1
            property bool lastFavourite: false

            function setSearchFilter(nodeType, category, createdWithin, favouritesOnly) {
                calls += 1;
                lastType = nodeType;
                lastCategory = category;
                lastTime = createdWithin;
                lastFavourite = favouritesOnly;
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

    function test_clearFiltersWaitsForApplyLikeEveryOtherControl() {
        const f = makePopup();

        pick(f.popup, SearchNodeType.Files, SearchCategory.Audio, SearchTimeWindow.Any, false);
        f.popup.apply();
        f.popup.clearPending();

        compare(f.nav.calls, 1);
        verify(f.popup.filterActive);
        verify(!f.popup.pendingActive);

        f.popup.apply();

        compare(f.nav.calls, 2);
        compare(f.nav.lastType, SearchNodeType.Any);
        compare(f.nav.lastCategory, SearchCategory.Any);
        compare(f.nav.lastFavourite, false);
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

    function test_pickingFoldersDropsTheCategory() {
        const f = makePopup();

        f.popup.categorySelector.currentIndex = SearchCategory.Photo;
        f.popup.typeSelector.currentIndex = SearchNodeType.Folders;
        f.popup.typeSelector.activated(SearchNodeType.Folders);

        compare(f.popup.categorySelector.currentIndex, SearchCategory.Any);
        compare(f.nav.calls, 0);
    }

    function test_resetMirrorsCppDroppingTheFilter() {
        const f = makePopup();

        pick(f.popup, SearchNodeType.Files, SearchCategory.Video, SearchTimeWindow.PastMonth, true);
        f.popup.apply();
        f.popup.reset();

        // C++ has already dropped it (searchCleared), so pushing here would re-run a
        // search the navigation just cancelled.
        compare(f.nav.calls, 1);
        verify(!f.popup.filterActive);
        verify(!f.popup.pendingActive);
        compare(f.popup.typeSelector.currentIndex, SearchNodeType.Any);
        compare(f.popup.favouriteSelector.checked, false);
    }
}
