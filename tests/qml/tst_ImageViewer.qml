import QtQuick
import QtTest
import MegaExplorer

// Covers the viewer window's own logic, which is the part no C++ test can reach:
// ViewerControllerTest.cpp owns canView()/sourceUrl(), and what is left here --
// refusing a name the controller cannot view, standing several windows at once,
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

    // A window per double-click: two viewers stand at once, each on its own image,
    // and closing one leaves the other untouched.
    function test_viewersStandAndCloseIndependently() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const first = makeViewer(controller);
        const second = makeViewer(controller);

        first.open(7, "photo.jpg");
        second.open(8, "other.jpg");
        verify(first.showing);
        verify(second.showing);
        compare(String(first.source), "http://127.0.0.1:1/7/f.jpg");
        compare(controller.urlCallCount, 2);

        first.close();
        tryVerify(() => !first.showing);
        verify(second.showing);
        compare(second.currentName, "other.jpg");
        compare(String(second.source), "http://127.0.0.1:1/8/f.jpg");
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
