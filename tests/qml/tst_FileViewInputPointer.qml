import QtQuick
import QtTest
import MegaExplorer

// The half of FileViewInput tst_FileViewInput.qml deliberately leaves out: real
// pointer delivery. It needs a shown window and synthetic mouse events, and it
// is the only thing that can catch the defect it exists for -- hover and taps
// landing on a row *below* the pointer, by exactly the scroll offset, because a
// handler declared `parent: <a Flickable>` is installed on that Flickable's
// contentItem and so reports positions in content coordinates
// (docs/investigations/VIEW_HIT_TEST_OFFSET_INVESTIGATION.md).
//
// Separate file rather than a case in tst_FileViewInput.qml: that one asserts
// on `hovered` being false *because* there is no window, which a shown window
// would quietly invalidate.
//
// The fixture stands in for a view rather than instantiating one: rowAtPos is
// specified in view coordinates, so a fake host that adds contentY itself is
// exactly what FileGridView/FileTableView do with mapFromItem(), without the
// controllers a real view would need.
TestCase {
    id: testCase
    name: "FileViewInputPointer"
    width: 200
    height: 200
    visible: true
    when: windowShown

    readonly property int rowHeight: 20
    // 20 rows scrolled past, so a position mistaken for content coordinates
    // lands 20 rows too low rather than merely one.
    readonly property int scrolledRows: 20

    property point lastHitPos: Qt.point(-1, -1)
    property var selectRowCalls: []

    Flickable {
        id: flickView
        anchors.fill: parent
        contentWidth: 200
        contentHeight: 2000
    }

    Component {
        id: inputComponent
        FileViewInput {}
    }

    function makeInput() {
        testCase.lastHitPos = Qt.point(-1, -1);
        testCase.selectRowCalls = [];
        flickView.contentY = testCase.scrolledRows * testCase.rowHeight;

        const model = {
            "cursorRow": function () {
                return -1;
            },
            "selectedEntries": function () {
                return [];
            },
            "selectAll": function () {},
            "clearSelection": function () {},
            "selectRow": function (row) {
                testCase.selectRowCalls.push(row);
            },
            "moveCursor": function () {}
        };

        const input = createTemporaryObject(inputComponent, testCase, {
                                                "view": flickView,
                                                "navController": {
                                                    "fileListModel": model,
                                                    "currentHandle": 7,
                                                    "atRoot": false
                                                },
                                                "mutController": {
                                                    "paste": function () {},
                                                    "renameEntry": function () {},
                                                    "canPaste": function () {
                                                        return false;
                                                    }
                                                },
                                                "clipboard": {
                                                    "cut": function () {},
                                                    "copy": function () {}
                                                },
                                                // The host's contract: pos is in view
                                                // coordinates, so the host is what adds
                                                // contentY.
                                                "rowAtPos": function (pos) {
                                                    testCase.lastHitPos = pos;
                                                    return Math.floor((pos.y + flickView.contentY)
                                                                      / testCase.rowHeight);
                                                },
                                                "takeFocus": function () {},
                                                "revealRow": function () {},
                                                "arrowColumns": 1,
                                                "horizontalArrows": false
                                            });
        verify(input !== null);
        return input;
    }

    // Row 22: 20 scrolled past plus the 3rd row of the viewport (y 50 / 20).
    readonly property int expectedRow: 22

    function test_hoverResolvesTheRowUnderThePointer() {
        const input = makeInput();
        mouseMove(testCase, 100, 50);
        tryCompare(input, "hoverRow", testCase.expectedRow);
        // The hit test is specified in view coordinates; 450 here would mean the
        // handler's content-coordinate position reached it unconverted.
        fuzzyCompare(testCase.lastHitPos.y, 50, 0.01);
    }

    function test_tapSelectsTheRowUnderThePointer() {
        const input = makeInput();
        mouseClick(testCase, 100, 50);
        tryVerify(() => testCase.selectRowCalls.length === 1);
        compare(testCase.selectRowCalls[0], testCase.expectedRow);
    }
}
