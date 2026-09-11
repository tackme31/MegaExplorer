import QtQuick
import QtTest
import MegaExplorer

// Covers the archive viewer window's own logic: refusing a name that belongs to
// another viewer, turning a row activation into a folder change or an extraction, the
// Up and extract buttons and the breadcrumb, and what it says while loading or after a
// failure.
// ViewerControllerTest.cpp owns the listing itself (openArchive, ArchiveBrowser).
TestCase {
    id: testCase
    name: "ArchiveViewer"
    when: windowShown
    visible: true

    // Mirrors ArchiveBrowser: a two-level archive, walked the way the C++ one is.
    Component {
        id: fakeBrowserComponent
        QtObject {
            id: fake
            property int state: ArchiveBrowser.Ready
            property int reason: ArchiveBrowser.NoReason
            property var path: []
            property var entries: fake.rowsFor(fake.path)
            signal changed

            function row(name, isDirectory, blockedReason) {
                const entry = {
                    name: name,
                    isDirectory: isDirectory,
                    formattedSize: isDirectory ? "" : "1.4 kB"
                };
                if (!isDirectory) {
                    entry.extractable = blockedReason === "";
                    entry.blockedReason = blockedReason;
                }
                return entry;
            }
            function rowsFor(path) {
                if (path.length === 0)
                    return [fake.row("assets", true), fake.row("docs", true), fake.row(
                                "readme.txt", false, "")];
                return [fake.row("guide.txt", false, "encrypted")];
            }
            function openFolder(name) {
                if (fake.path.length === 0 && (name === "assets" || name === "docs")) {
                    fake.path = [name];
                    fake.changed();
                }
            }
            function goUp() {
                fake.goToDepth(fake.path.length - 1);
            }
            function goToDepth(depth) {
                if (depth < 0 || depth > fake.path.length)
                    return;
                fake.path = fake.path.slice(0, depth);
                fake.changed();
            }
        }
    }

    Component {
        id: fakeControllerComponent
        QtObject {
            property int openCount: 0
            property var lastSize: undefined
            property var browserState: ArchiveBrowser.Ready
            property var browserReason: ArchiveBrowser.NoReason
            function viewerKind(name) {
                const lower = String(name).toLowerCase();
                if (lower.endsWith(".zip"))
                    return "archive";
                return lower.endsWith(".mp3") ? "audio" : "";
            }
            function openArchive(handle, sizeBytes, owner) {
                openCount++;
                lastSize = sizeBytes;
                return fakeBrowserComponent.createObject(owner, {
                                                             state: browserState,
                                                             reason: browserReason
                                                         });
            }
        }
    }

    Component {
        id: fakeDownloadsComponent
        QtObject {
            property int calls: 0
            property var lastBrowser: null
            property string lastName: ""
            function extractArchiveEntry(browser, name) {
                calls++;
                lastBrowser = browser;
                lastName = name;
            }
        }
    }

    Component {
        id: viewerComponent
        ArchiveViewer {}
    }

    function makeViewer(controller) {
        const viewer = createTemporaryObject(viewerComponent, testCase, {
                                                 controller: controller,
                                                 width: 480,
                                                 height: 360
                                             });
        verify(viewer);
        return viewer;
    }

    function entryList(viewer) {
        return findChild(viewer.contentItem, "entryList");
    }

    function upButton(viewer) {
        return findChild(viewer.contentItem, "upButton");
    }

    function statusLabel(viewer) {
        return findChild(viewer.contentItem, "statusLabel");
    }

    function extractButton(viewer) {
        return findChild(viewer.contentItem, "extractButton");
    }

    function makeExtractingViewer(downloads) {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);
        viewer.downloads = downloads;
        viewer.open(7, "bundle.zip", 4096);
        tryCompare(entryList(viewer), "count", 3);
        return viewer;
    }

    function test_opensOnlyWhatBelongsToTheArchiveViewer() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);

        viewer.open(1, "song.mp3", 100);
        verify(!viewer.showing);
        compare(controller.openCount, 0);
    }

    // Open as: the extension is not asked -- a .docx is a zip, for one.
    function test_forcedOpenShowsAFileTheNameDoesNotClaim() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);

        viewer.open(7, "report.docx", 4096, true);
        verify(viewer.showing);
        compare(viewer.title, "report.docx");
        compare(controller.openCount, 1);
        compare(controller.lastSize, 4096);
    }

    function test_openShowsTheArchiveRootAndPassesItsSize() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);

        viewer.open(7, "bundle.zip", 4096);
        verify(viewer.showing);
        compare(viewer.title, "bundle.zip");
        compare(controller.lastSize, 4096);
        tryCompare(entryList(viewer), "count", 3);
        verify(!statusLabel(viewer).visible);
        verify(!upButton(viewer).enabled);
        compare(viewer.crumbs, ["bundle.zip"]);
    }

    function test_activatingAFolderRowOpensItAndAFileRowDoesNothing() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);
        viewer.open(7, "bundle.zip", 4096);

        viewer.openRow(2);
        compare(viewer.browser.path, []);

        viewer.openRow(1);
        compare(viewer.browser.path, ["docs"]);
        compare(viewer.crumbs, ["bundle.zip", "docs"]);
        verify(upButton(viewer).enabled);
        tryCompare(entryList(viewer), "count", 1);
    }

    // Explorer's behaviour: coming back up leaves the folder just left current,
    // not the first row of its parent.
    function test_goingUpLandsOnTheFolderJustLeft() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);
        viewer.open(7, "bundle.zip", 4096);
        viewer.openRow(1);
        tryCompare(entryList(viewer), "count", 1);

        upButton(viewer).clicked();
        compare(viewer.browser.path, []);
        tryCompare(entryList(viewer), "count", 3);
        tryCompare(entryList(viewer), "currentIndex", 1);
        verify(!upButton(viewer).enabled);
    }

    function test_theBreadcrumbRootReturnsToTheTop() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);
        viewer.open(7, "bundle.zip", 4096);
        viewer.openRow(0);

        viewer.goToDepth(0);
        compare(viewer.browser.path, []);
        compare(viewer.crumbs, ["bundle.zip"]);
    }

    function test_aFailureSaysWhyInsteadOfListing() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        controller.browserState = ArchiveBrowser.Failed;
        controller.browserReason = ArchiveBrowser.Empty;
        const viewer = makeViewer(controller);

        viewer.open(7, "empty.zip", 22);
        verify(viewer.failed);
        verify(statusLabel(viewer).visible);
        compare(statusLabel(viewer).text, "This archive is empty");
        verify(!entryList(viewer).visible);
        verify(!upButton(viewer).enabled);
    }

    function test_loadingShowsNeitherRowsNorAFailure() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        controller.browserState = ArchiveBrowser.Loading;
        const viewer = makeViewer(controller);

        viewer.open(7, "big.zip", 1 << 20);
        verify(viewer.loading);
        verify(!statusLabel(viewer).visible);
        verify(!entryList(viewer).visible);
    }

    // A window per double-click: two stand at once, each in a folder of its own.
    function test_viewersStandAndMoveIndependently() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const first = makeViewer(controller);
        const second = makeViewer(controller);
        first.open(7, "bundle.zip", 4096);
        second.open(7, "bundle.zip", 4096);

        first.openRow(1);
        compare(first.browser.path, ["docs"]);
        compare(second.browser.path, []);

        first.close();
        verify(!first.showing);
        compare(first.browser, null);
        verify(second.showing);
    }

    function test_activatingAFileRowExtractsIt() {
        const downloads = createTemporaryObject(fakeDownloadsComponent, testCase);
        const viewer = makeExtractingViewer(downloads);

        viewer.openRow(2);

        compare(downloads.calls, 1);
        compare(downloads.lastName, "readme.txt");
        verify(downloads.lastBrowser === viewer.browser);
        compare(viewer.browser.path, []);
    }

    function test_theExtractButtonFollowsTheCurrentRow() {
        const downloads = createTemporaryObject(fakeDownloadsComponent, testCase);
        const viewer = makeExtractingViewer(downloads);
        const button = extractButton(viewer);
        verify(button.visible);

        entryList(viewer).currentIndex = 0;
        verify(!button.enabled);
        entryList(viewer).currentIndex = 2;
        verify(button.enabled);
        button.clicked();
        compare(downloads.calls, 1);
        compare(downloads.lastName, "readme.txt");
    }

    // An encrypted entry keeps its row but cannot be extracted, from either entry point.
    function test_aBlockedEntryIsListedButNotExtracted() {
        const downloads = createTemporaryObject(fakeDownloadsComponent, testCase);
        const viewer = makeExtractingViewer(downloads);
        viewer.openRow(1);
        tryCompare(entryList(viewer), "count", 1);
        tryCompare(entryList(viewer), "currentIndex", 0);

        verify(!extractButton(viewer).enabled);
        verify(findChild(entryList(viewer), "blockedLabel").visible);
        viewer.openRow(0);
        compare(downloads.calls, 0);
    }

    function test_withoutDownloadsTheViewerOnlyBrowses() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);
        viewer.open(7, "bundle.zip", 4096);

        verify(!extractButton(viewer).visible);
        viewer.openRow(2);
        compare(viewer.browser.path, []);
    }
}
