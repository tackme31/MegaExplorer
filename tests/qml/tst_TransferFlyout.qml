import QtQuick
import QtTest
import MegaExplorer

// The flyout's own state machine: when it puts itself up, when it stays up, and
// what the count beside the bar says. TransferListModel supplies the four run
// properties (covered in tests/TransferListModelTest.cpp); this file drives them
// from a stub, so the wiring is checked without a MEGA session.
TestCase {
    id: testCase
    name: "TransferFlyout"
    width: 600
    height: 400

    Component {
        id: stubComponent

        QtObject {
            property bool runActive: false
            property int runTotal: 0
            property int runFinished: 0
            property real runProgress: 0
        }
    }

    Component {
        id: flyoutComponent

        TransferFlyout {}
    }

    function makeFlyout(stub) {
        return createTemporaryObject(flyoutComponent, testCase, {
                                         "transfers": stub
                                     });
    }

    function test_startsHiddenAndClaimsNoCorner() {
        const stub = createTemporaryObject(stubComponent, testCase);
        const flyout = makeFlyout(stub);

        verify(!flyout.shown);
        compare(flyout.reservedHeight, 0);
    }

    function test_aStartingRunPutsItUp() {
        const stub = createTemporaryObject(stubComponent, testCase);
        const flyout = makeFlyout(stub);

        stub.runTotal = 41;
        stub.runFinished = 9;
        stub.runActive = true;

        verify(flyout.shown);
        // Auto-opened, so it is allowed to take itself away once the run settles.
        verify(flyout.autoOpened);
        compare(flyout.summaryText(), "9 / 41");
        // The toasts move up the moment the bar is asked for, not when its fade
        // finishes -- ToastStack.qml reads this.
        verify(flyout.reservedHeight > 0);
    }

    // Closing the bar is not cancelling: nothing on the model is touched, so the
    // run carries on with the flyout out of the way.
    function test_hidingLeavesTheRunRunning() {
        const stub = createTemporaryObject(stubComponent, testCase);
        const flyout = makeFlyout(stub);

        stub.runTotal = 2;
        stub.runActive = true;
        flyout.hide();

        verify(!flyout.shown);
        verify(stub.runActive);
        compare(stub.runTotal, 2);
    }

    // The More menu's route in. It must not be armed for the idle auto-hide, or
    // choosing "Transfers" with nothing in flight would be a brief flash.
    function test_openingFromTheMenuIsNotAutoHidden() {
        const stub = createTemporaryObject(stubComponent, testCase);
        const flyout = makeFlyout(stub);

        flyout.show();

        verify(flyout.shown);
        verify(!flyout.autoOpened);
        compare(flyout.summaryText(), "No transfers");
    }

    function test_summaryTextFollowsTheRun_data() {
        return [
                    {
                        tag: "nothing",
                        total: 0,
                        finished: 0,
                        expected: "No transfers"
                    },
                    {
                        tag: "started",
                        total: 41,
                        finished: 0,
                        expected: "0 / 41"
                    },
                    {
                        tag: "done",
                        total: 41,
                        finished: 41,
                        expected: "41 / 41"
                    },
                ];
    }

    function test_summaryTextFollowsTheRun(data) {
        const stub = createTemporaryObject(stubComponent, testCase);
        const flyout = makeFlyout(stub);

        stub.runTotal = data.total;
        stub.runFinished = data.finished;

        compare(flyout.summaryText(), data.expected);
    }
}
