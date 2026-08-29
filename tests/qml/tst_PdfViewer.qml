import QtQuick
import QtTest
import MegaExplorer

// Covers the PDF viewer window's own logic, which is the part no C++ test can reach:
// ViewerControllerTest.cpp owns viewerKind()/sourceUrl(), and what is left here --
// refusing a name that belongs to another viewer, standing several windows at once,
// dropping the document when the window hides -- lives in this file's QML.
//
// Nothing here asserts on rendering: the URL points at a port with no server behind
// it, so PdfPageItem's fetch fails and the page count stays zero. Paging is exercised
// against that empty document, where the clamp is the whole of the behaviour.
TestCase {
    id: testCase
    name: "PdfViewer"
    when: windowShown
    visible: true

    // Mirrors ViewerController's two invokables, narrowed to what these cases use.
    Component {
        id: fakeControllerComponent
        QtObject {
            property int urlCallCount: 0
            function viewerKind(name) {
                const lower = String(name).toLowerCase();
                if (lower.endsWith(".pdf"))
                    return "pdf";
                return lower.endsWith(".jpg") ? "image" : "";
            }
            function sourceUrl(handle) {
                urlCallCount++;
                return "http://127.0.0.1:1/" + handle + "/f.pdf";
            }
        }
    }

    Component {
        id: viewerComponent
        PdfViewer {}
    }

    function makeViewer(controller) {
        const viewer = createTemporaryObject(viewerComponent, testCase, {
                                                 controller: controller,
                                                 width: 320,
                                                 height: 320
                                             });
        verify(viewer);
        return viewer;
    }

    function test_opensOnlyWhatBelongsToThePdfViewer() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);

        viewer.open(1, "photo.jpg");
        verify(!viewer.showing);
        compare(controller.urlCallCount, 0);
    }

    function test_openShowsTheDocumentAndAsksForItsUrl() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);

        viewer.open(7, "manual.pdf");
        verify(viewer.showing);
        compare(viewer.currentName, "manual.pdf");
        compare(String(viewer.source), "http://127.0.0.1:1/7/f.pdf");
    }

    // A window per double-click, as for images and videos: two viewers stand at once,
    // each on its own file, and closing one leaves the other untouched.
    function test_viewersStandAndCloseIndependently() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const first = makeViewer(controller);
        const second = makeViewer(controller);

        first.open(7, "manual.pdf");
        second.open(8, "other.pdf");
        verify(first.showing);
        verify(second.showing);
        compare(controller.urlCallCount, 2);

        first.close();
        tryVerify(() => !first.showing);
        verify(second.showing);
        compare(second.currentName, "other.pdf");
        compare(String(second.source), "http://127.0.0.1:1/8/f.pdf");
    }

    // Hiding is the single teardown path, so the document is dropped whichever way the
    // window was dismissed -- otherwise the whole file stays in memory behind a window
    // that is gone.
    function test_hidingReleasesTheDocument() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);

        viewer.open(1, "manual.pdf");
        viewer.close();
        tryVerify(() => !viewer.showing);
        compare(String(viewer.source), "");
        compare(viewer.currentName, "");
    }

    // Paging past either end is the caller's normal way of asking for the next page,
    // so it has to stop rather than leave the item on a page it cannot render.
    function test_pagingStaysWithinTheDocument() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const viewer = makeViewer(controller);

        viewer.open(1, "manual.pdf");
        viewer.nextPage();
        viewer.nextPage();
        viewer.previousPage();
        verify(viewer.showing);
    }
}
