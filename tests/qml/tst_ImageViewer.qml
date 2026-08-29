import QtQuick
import QtTest
import MegaExplorer

// Covers the viewer window's own logic, which is the part no C++ test can reach:
// ViewerControllerTest.cpp owns canView()/sourceUrl(), and what is left here --
// refusing a name the controller cannot view, replacing what one window shows,
// releasing the image when the window hides -- lives in this file's QML.
//
// The controller is a fake rather than the real type: ViewerController would open
// the SDK's local HTTP server.
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

    Component {
        id: viewerComponent
        ImageViewer {}
    }

    function makeViewer(controller) {
        const viewer = createTemporaryObject(viewerComponent, testCase, {
                                                 controller: controller,
                                                 width: 320,
                                                 height: 240
                                             });
        verify(viewer);
        return viewer;
    }

    function test_opensOnlyWhatTheControllerCanView() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);

        viewer.open(1, "clip.mp4");
        verify(!viewer.showing);
        compare(controller.urlCallCount, 0);
    }

    function test_openShowsTheImageAndAsksForItsUrl() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);

        viewer.open(7, "photo.jpg");
        verify(viewer.showing);
        compare(viewer.currentName, "photo.jpg");
        compare(String(viewer.source), "http://127.0.0.1:1/7/f.jpg");
    }

    // One window, reused: a second double-click replaces what it shows rather than
    // opening a second one.
    function test_openAgainReplacesWhatIsShown() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);

        viewer.open(7, "photo.jpg");
        viewer.open(8, "other.jpg");
        verify(viewer.showing);
        compare(viewer.currentName, "other.jpg");
        compare(String(viewer.source), "http://127.0.0.1:1/8/f.jpg");
        compare(controller.urlCallCount, 2);
    }

    // Hiding is the single teardown path, so the megabytes the decoded original
    // holds go back whichever way the window was dismissed.
    function test_hidingReleasesTheImage() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);

        viewer.open(1, "a.jpg");
        viewer.actualSize = true;
        viewer.close();
        tryVerify(() => !viewer.showing);
        compare(String(viewer.source), "");
        compare(viewer.currentName, "");
        verify(!viewer.actualSize);
    }
}
