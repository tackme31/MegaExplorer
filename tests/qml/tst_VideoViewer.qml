import QtQuick
import QtTest
import MegaExplorer

// Covers the video viewer window's own logic, which is the part no C++ test can
// reach: ViewerControllerTest.cpp owns viewerKind()/sourceUrl(), and what is left
// here -- refusing a name that belongs to another viewer, standing several windows
// at once, releasing the stream when the window hides -- lives in this file's QML.
//
// Nothing here asserts on playback: the URL points at a port with no server behind
// it, so the player errors out asynchronously and the states it passes through are
// the backend's business, not this file's.
TestCase {
    id: testCase
    name: "VideoViewer"
    when: windowShown
    visible: true

    // Mirrors ViewerController's two invokables, narrowed to what these cases use.
    Component {
        id: fakeControllerComponent
        QtObject {
            property int urlCallCount: 0
            function viewerKind(name) {
                const lower = String(name).toLowerCase();
                if (lower.endsWith(".mp4"))
                    return "video";
                return lower.endsWith(".jpg") ? "image" : "";
            }
            function sourceUrl(handle) {
                urlCallCount++;
                return "http://127.0.0.1:1/" + handle + "/f.mp4";
            }
        }
    }

    Component {
        id: viewerComponent
        VideoViewer {}
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

    function test_opensOnlyWhatBelongsToTheVideoViewer() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);

        viewer.open(1, "photo.jpg");
        verify(!viewer.showing);
        compare(controller.urlCallCount, 0);
    }

    function test_openShowsTheVideoAndAsksForItsUrl() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);

        viewer.open(7, "clip.mp4");
        verify(viewer.showing);
        compare(viewer.currentName, "clip.mp4");
        compare(String(viewer.source), "http://127.0.0.1:1/7/f.mp4");
    }

    // A window per double-click, as for images: two viewers stand at once, each on
    // its own file, and closing one leaves the other untouched.
    function test_viewersStandAndCloseIndependently() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const first = makeViewer(controller);
        const second = makeViewer(controller);

        first.open(7, "clip.mp4");
        second.open(8, "other.mp4");
        verify(first.showing);
        verify(second.showing);
        compare(controller.urlCallCount, 2);

        first.close();
        tryVerify(() => !first.showing);
        verify(second.showing);
        compare(second.currentName, "other.mp4");
        compare(String(second.source), "http://127.0.0.1:1/8/f.mp4");
    }

    // Hiding is the single teardown path, so the stream is dropped whichever way the
    // window was dismissed -- otherwise the backend keeps pulling from the local
    // HTTP server after the window is gone.
    function test_hidingReleasesTheStream() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);

        viewer.open(1, "clip.mp4");
        viewer.close();
        tryVerify(() => !viewer.showing);
        compare(String(viewer.source), "");
        compare(viewer.currentName, "");
    }

    SignalSpy {
        id: volumeSpy
        signalName: "volumeRequested"
    }

    SignalSpy {
        id: muteSpy
        signalName: "muteToggleRequested"
    }

    // The window plays at a level it does not own: Main.qml holds the app-wide,
    // persisted copy, so the transport bar asks and takes the answer back down the
    // binding. Writing the property here instead would leave a second open viewer
    // showing the old level.
    function test_theTransportBarAsksForVolumeRatherThanWritingIt() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);
        viewer.open(1, "clip.mp4");

        const slider = findChild(viewer, "volumeSlider");
        const mute = findChild(viewer, "muteButton");
        verify(slider);
        verify(mute);

        // Handed down.
        viewer.volume = 0.4;
        compare(slider.value, 0.4);

        // Asked back up, and nothing changes here until the answer arrives.
        volumeSpy.target = viewer;
        slider.value = 0.25;
        slider.moved();
        compare(volumeSpy.count, 1);
        compare(volumeSpy.signalArguments[0][0], 0.25);
        compare(viewer.volume, 0.4);

        muteSpy.target = viewer;
        mute.clicked();
        compare(muteSpy.count, 1);
        compare(viewer.muted, false);
    }

    function test_muteDisablesTheVolumeSlider() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);
        viewer.open(1, "clip.mp4");

        const slider = findChild(viewer, "volumeSlider");
        verify(slider.enabled);
        viewer.muted = true;
        verify(!slider.enabled);
    }

    function test_formatTimeReadsAsAClock() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);

        compare(viewer.formatTime(0), "0:00");
        compare(viewer.formatTime(9000), "0:09");
        compare(viewer.formatTime(605000), "10:05");
        compare(viewer.formatTime(3723000), "1:02:03");
        // Duration is 0 before the media loads and -1 on some failures; neither may
        // reach the label as a negative clock.
        compare(viewer.formatTime(-1), "0:00");
    }
}
