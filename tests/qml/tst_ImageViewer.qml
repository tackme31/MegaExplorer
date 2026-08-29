import QtQuick
import QtTest
import MegaExplorer

// Covers the viewer's row walk, which is the part no C++ test can reach:
// ViewerControllerTest.cpp owns canView()/sourceUrl(), and everything else the
// viewer does -- picking the neighbour to show, surviving a listing that changed
// under it, releasing the image on close -- lives in this file's QML.
//
// Both injected objects are fakes rather than the real types: FileListModel needs
// C++ rows this harness has no way to build, and ViewerController would open the
// SDK's local HTTP server.
TestCase {
    id: testCase
    name: "ImageViewer"
    when: windowShown
    visible: true

    // Mirrors ViewerController's two invokables. canView() answers on the same
    // basis the real one does (extension only), narrowed to what these cases use.
    Component {
        id: fakeControllerComponent
        QtObject {
            property int urlCallCount: 0
            function canView(name) {
                return String(name).toLowerCase().endsWith(".jpg");
            }
            function sourceUrl(handle) {
                urlCallCount++;
                return "http://127.0.0.1:1/" + handle + "/f.jpg";
            }
        }
    }

    // Mirrors the three FileListModel members the viewer touches. `rows` is a
    // plain array of {handle, name, isFolder}, in the order the listing shows.
    Component {
        id: fakeModelComponent
        QtObject {
            property var rows: []
            readonly property int count: rows.length
            function rowForHandle(handle) {
                for (let i = 0; i < rows.length; ++i) {
                    if (rows[i].handle === handle)
                        return i;
                }
                return -1;
            }
            function entryAt(row) {
                return (row < 0 || row >= rows.length) ? ({}) : rows[row];
            }
        }
    }

    Component {
        id: viewerComponent
        ImageViewer {}
    }

    function makeModel(rows) {
        return createTemporaryObject(fakeModelComponent, testCase, {
                                         rows: rows
                                     });
    }

    function makeViewer(controller) {
        const viewer = createTemporaryObject(viewerComponent, testCase, {
                                                 controller: controller,
                                                 width: 400,
                                                 height: 300
                                             });
        verify(viewer);
        return viewer;
    }

    function test_opensOnlyWhatTheControllerCanView() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const model = makeModel([
                                    {
                                        handle: 1,
                                        name: "clip.mp4",
                                        isFolder: false
                                    }
                                ]);
        const viewer = makeViewer(controller);

        viewer.open(model, 1, "clip.mp4");
        verify(!viewer.showing);
        compare(controller.urlCallCount, 0);
    }

    function test_openShowsTheRowAndAsksForItsUrl() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const model = makeModel([
                                    {
                                        handle: 7,
                                        name: "photo.jpg",
                                        isFolder: false
                                    }
                                ]);
        const viewer = makeViewer(controller);

        viewer.open(model, 7, "photo.jpg");
        verify(viewer.showing);
        compare(viewer.currentName, "photo.jpg");
        compare(String(viewer.source), "http://127.0.0.1:1/7/f.jpg");
    }

    function test_openIgnoresAHandleTheListingDoesNotHold() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const model = makeModel([
                                    {
                                        handle: 7,
                                        name: "photo.jpg",
                                        isFolder: false
                                    }
                                ]);
        const viewer = makeViewer(controller);

        viewer.open(model, 99, "photo.jpg");
        verify(!viewer.showing);
    }

    function test_stepSkipsFoldersAndUnviewableFiles() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const model = makeModel([
                                    {
                                        handle: 1,
                                        name: "a.jpg",
                                        isFolder: false
                                    },
                                    {
                                        handle: 2,
                                        name: "sub",
                                        isFolder: true
                                    },
                                    {
                                        handle: 3,
                                        name: "notes.txt",
                                        isFolder: false
                                    },
                                    {
                                        handle: 4,
                                        name: "b.jpg",
                                        isFolder: false
                                    }
                                ]);
        const viewer = makeViewer(controller);

        viewer.open(model, 1, "a.jpg");
        viewer.step(1);
        compare(viewer.currentName, "b.jpg");
        compare(viewer.currentRow, 3);

        viewer.step(-1);
        compare(viewer.currentName, "a.jpg");
        compare(viewer.currentRow, 0);
    }

    function test_stepAtTheEndStaysOnTheCurrentRow() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const model = makeModel([
                                    {
                                        handle: 1,
                                        name: "a.jpg",
                                        isFolder: false
                                    }
                                ]);
        const viewer = makeViewer(controller);

        viewer.open(model, 1, "a.jpg");
        viewer.step(1);
        verify(viewer.showing);
        compare(viewer.currentName, "a.jpg");
        viewer.step(-1);
        compare(viewer.currentName, "a.jpg");
    }

    // The listing can be replaced underneath the viewer: the address bar and
    // breadcrumb sit in the window header, which the overlay does not cover.
    function test_stepClosesWhenTheShownHandleIsGone() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const model = makeModel([
                                    {
                                        handle: 1,
                                        name: "a.jpg",
                                        isFolder: false
                                    },
                                    {
                                        handle: 2,
                                        name: "b.jpg",
                                        isFolder: false
                                    }
                                ]);
        const viewer = makeViewer(controller);
        const closedSpy = createTemporaryObject(spyComponent, testCase, {
                                                    target: viewer
                                                });

        viewer.open(model, 1, "a.jpg");
        model.rows = [
                    {
                        handle: 5,
                        name: "elsewhere.jpg",
                        isFolder: false
                    }
                ];
        viewer.step(1);
        verify(!viewer.showing);
        compare(closedSpy.count, 1);
    }

    // Re-resolved from the handle, not from currentRow: a same-model refill that
    // reorders the rows would otherwise walk from a stale index.
    function test_stepReResolvesTheRowFromTheHandle() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const model = makeModel([
                                    {
                                        handle: 1,
                                        name: "a.jpg",
                                        isFolder: false
                                    },
                                    {
                                        handle: 2,
                                        name: "b.jpg",
                                        isFolder: false
                                    }
                                ]);
        const viewer = makeViewer(controller);

        viewer.open(model, 2, "b.jpg");
        compare(viewer.currentRow, 1);
        // Same rows, opposite order: handle 2 is now first, so "next" is handle 1.
        model.rows = [
                    {
                        handle: 2,
                        name: "b.jpg",
                        isFolder: false
                    },
                    {
                        handle: 1,
                        name: "a.jpg",
                        isFolder: false
                    }
                ];
        viewer.step(1);
        compare(viewer.currentName, "a.jpg");
        compare(viewer.currentRow, 1);
    }

    function test_closeReleasesTheImageAndSignals() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const model = makeModel([
                                    {
                                        handle: 1,
                                        name: "a.jpg",
                                        isFolder: false
                                    }
                                ]);
        const viewer = makeViewer(controller);
        const closedSpy = createTemporaryObject(spyComponent, testCase, {
                                                    target: viewer
                                                });

        viewer.open(model, 1, "a.jpg");
        viewer.close();
        verify(!viewer.showing);
        compare(String(viewer.source), "");
        compare(viewer.currentName, "");
        compare(closedSpy.count, 1);

        // Idempotent: Main.qml calls close() on every tab switch, viewer up or not.
        viewer.close();
        compare(closedSpy.count, 1);
    }

    Component {
        id: spyComponent
        SignalSpy {
            signalName: "closed"
        }
    }
}
