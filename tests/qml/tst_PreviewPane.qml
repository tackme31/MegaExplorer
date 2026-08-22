import QtQuick
import QtTest
import MegaExplorer

// What this covers is the pane's half of the contract with PreviewController:
// which child is visible in each state, that the reason enum reaches the right
// sentence, and that hiding the pane tells the controller to stop.
//
// The fetch driver's other two triggers (selection changed, active tab changed)
// are not covered: both need a FileListModel with real rows, which is C++ state
// this harness has no way to build. PreviewControllerTest.cpp covers the
// generation behaviour those triggers exist to produce.
TestCase {
    id: testCase
    name: "PreviewPane"
    when: windowShown
    // TestCase declares itself visible: false, and Item.visible reads back the
    // *effective* value -- every child of an invisible parent reports false, which
    // is exactly what these tests assert on.
    visible: true

    // A QtObject, not a JS object literal: PreviewPane binds against this as a
    // property of an Item, and its `changed` signal has to be a real signal or
    // none of the bindings below would ever re-evaluate. The property set must
    // match src/qml/PreviewController.h's, since a mismatch is what this file
    // exists to catch.
    Component {
        id: fakeControllerComponent
        QtObject {
            property int state: PreviewController.Empty
            property int kind: PreviewController.NoKind
            property string imageSource: ""
            property string text: ""
            property var archiveEntries: []
            property int reason: PreviewController.NoReason

            // PreviewPane listens for this to drive the text swap; the per-property
            // Changed signals above do not reach that handler.
            signal changed

            property int clearCount: 0
            function clear() {
                clearCount++;
            }
            function showSelection(handle, name, sizeBytes, isFolder) {
            }
        }
    }

    Component {
        id: paneComponent
        PreviewPane {}
    }

    function makePane(controller) {
        // A required property left out would make this return null.
        const pane = createTemporaryObject(paneComponent, testCase, {
                                               controller: controller,
                                               currentPane: null,
                                               width: 300,
                                               height: 400
                                           });
        verify(pane !== null);
        return pane;
    }

    // Located by a property only that child has, rather than by type name: a QML
    // Controls type reports itself as Label_QMLTYPE_<n>, and rather than pinning
    // that generated name the tests key on running/fillMode/wrapMode.
    function childWith(pane, distinguishingProperty) {
        for (let i = 0; i < pane.children.length; ++i) {
            if (pane.children[i][distinguishingProperty] !== undefined)
                return pane.children[i];
        }
        return null;
    }

    // Depth-first, because the text view sits inside a frame inside a Flickable's
    // contentItem rather than directly under the pane.
    function findDeep(item, distinguishingProperty) {
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i];
            if (child[distinguishingProperty] !== undefined)
                return child;
            const found = findDeep(child, distinguishingProperty);
            if (found !== null)
                return found;
        }
        return null;
    }

    function busyIndicatorOf(pane) {
        return childWith(pane, "running");
    }
    function imageOf(pane) {
        return childWith(pane, "fillMode");
    }
    function labelOf(pane) {
        return childWith(pane, "wrapMode");
    }

    function test_loading_shows_only_the_busy_indicator() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const pane = makePane(controller);

        controller.state = PreviewController.Loading;

        const busy = busyIndicatorOf(pane);
        const image = imageOf(pane);
        verify(busy !== null);
        verify(image !== null);
        compare(busy.visible, true);
        compare(image.visible, false);
    }

    function test_ready_image_points_the_image_at_the_controllers_url() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const pane = makePane(controller);

        controller.imageSource = "image://megapreview/7";
        controller.kind = PreviewController.Image;
        controller.state = PreviewController.Ready;

        const image = imageOf(pane);
        compare(image.visible, true);
        compare(image.source.toString(), "image://megapreview/7");
        // Every preview is a fresh generation, so caching one could only pin bytes.
        compare(image.cache, false);
    }

    function test_ready_text_survives_a_long_to_short_swap() {
        // The regression this covers is the three-step assignment in showText():
        // dropping a scrolled long document straight onto a short one leaves
        // TextEdit's scene graph nodes where the old document had them and the pane
        // paints blank, with every property still reading correct.
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const pane = makePane(controller);

        let longText = "";
        for (let i = 0; i < 400; ++i)
            longText += "line " + i + "\n";
        controller.text = longText;
        controller.kind = PreviewController.Text;
        controller.state = PreviewController.Ready;
        controller.changed();

        const flick = findDeep(pane, "contentY");
        const area = findDeep(pane, "readOnly");
        verify(flick !== null);
        verify(area !== null);
        tryCompare(area, "text", longText);

        flick.contentY = 500;
        controller.text = "short";
        controller.changed();

        tryCompare(area, "text", "short");
        compare(flick.contentY, 0);
    }

    function test_ready_archive_lists_every_entry_indented_by_depth() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const pane = makePane(controller);

        // The rows are what PreviewController hands over: already flattened, already
        // ordered, and with the size already worded. A non-ASCII name is in here
        // because the decode from the archive's raw bytes is the step most likely to
        // regress and the pane is where it becomes visible.
        controller.archiveEntries = [
                    {
                        name: "日本語フォルダ",
                        depth: 0,
                        isDirectory: true,
                        formattedSize: ""
                    },
                    {
                        name: "メモ.txt",
                        depth: 1,
                        isDirectory: false,
                        formattedSize: "1.4 kB"
                    }
                ];
        controller.kind = PreviewController.Archive;
        controller.state = PreviewController.Ready;

        // "delegate" is a ListView-only property here: the text preview's Flickable
        // would answer to the scrolling ones.
        const list = findDeep(pane, "delegate");
        verify(list !== null);
        tryCompare(list, "count", 2);

        const folderRow = list.itemAtIndex(0).children[0];
        const fileRow = list.itemAtIndex(1).children[0];
        compare(folderRow.anchors.leftMargin, 0);
        compare(fileRow.anchors.leftMargin, Theme.tree.indent);

        // Row order inside the delegate: icon, name, size.
        compare(folderRow.children[0].text, Theme.glyph.folder);
        compare(fileRow.children[0].text, Theme.glyph.file);
        compare(folderRow.children[1].text, "日本語フォルダ");
        compare(fileRow.children[1].text, "メモ.txt");
        // A folder's size arrives empty rather than as a zero.
        compare(folderRow.children[2].text, "");
        compare(fileRow.children[2].text, "1.4 kB");
    }

    function test_a_second_archive_starts_at_the_top() {
        // Same class of bug as the text view's three-step swap: the ListView keeps
        // the scroll position it had, so the next zip opens part-way down.
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const pane = makePane(controller);

        let many = [];
        for (let i = 0; i < 60; ++i)
            many.push({
                          name: "entry" + i + ".txt",
                          depth: 0,
                          isDirectory: false,
                          formattedSize: "1 bytes"
                      });
        controller.archiveEntries = many;
        controller.kind = PreviewController.Archive;
        controller.state = PreviewController.Ready;
        controller.changed();

        const list = findDeep(pane, "delegate");
        tryCompare(list, "count", 60);
        list.contentY = 300;
        compare(list.contentY, 300);

        controller.archiveEntries = [{
                name: "only.txt",
                depth: 0,
                isDirectory: false,
                formattedSize: "1 bytes"
            }];
        controller.changed();

        tryCompare(list, "count", 1);
        compare(list.contentY, 0);
    }

    function test_unsupported_reason_data() {
        return [
                    {
                        tag: "unsupportedType",
                        reason: PreviewController.UnsupportedType,
                        expected: "No preview available for this file type"
                    },
                    {
                        tag: "tooLarge",
                        reason: PreviewController.TooLarge,
                        expected: "This file is too large to preview"
                    },
                    {
                        tag: "binaryContent",
                        reason: PreviewController.BinaryContent,
                        expected: "This file is not text"
                    },
                    {
                        tag: "archiveUnreadable",
                        reason: PreviewController.ArchiveUnreadable,
                        expected: "This archive could not be read"
                    },
                    {
                        tag: "archiveEmpty",
                        reason: PreviewController.ArchiveEmpty,
                        expected: "This archive is empty"
                    },
                    // NoPreviewAvailable is the common case and falls to the default arm.
                    {
                        tag: "noPreviewAvailable",
                        reason: PreviewController.NoPreviewAvailable,
                        expected: "No preview available"
                    }
                ];
    }

    function test_unsupported_reason(data) {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const pane = makePane(controller);

        controller.reason = data.reason;
        controller.state = PreviewController.Unsupported;

        const label = labelOf(pane);
        verify(label !== null);
        compare(label.visible, true);
        compare(label.text, data.expected);
    }

    function test_hiding_the_pane_stops_the_controller() {
        const controller = createTemporaryObject(fakeControllerComponent, testCase);
        const pane = makePane(controller);
        const before = controller.clearCount;

        pane.visible = false;

        // No model and no selection either way, so the only claim here is that
        // hiding drives refresh() at all -- a preview-off tab must cost no traffic.
        verify(controller.clearCount > before);
    }
}
