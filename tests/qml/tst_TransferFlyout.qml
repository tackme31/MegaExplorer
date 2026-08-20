import QtQuick
import QtTest
import MegaExplorer

// The flyout's own state machine: when it puts itself up, when it stays up, what
// the count beside the bar says, and what the expanded panel makes of the rows.
// TransferListModel supplies the four run properties and the rows (covered in
// tests/TransferListModelTest.cpp); this file drives both from a stub, so the
// wiring is checked without a MEGA session.
TestCase {
    id: testCase
    name: "TransferFlyout"
    width: 600
    height: 400

    // A ListModel rather than a QtObject: the flyout reads the run properties off
    // this same object *and* hands it to a ListView, which TransferListModel does
    // in one class too.
    Component {
        id: stubComponent

        ListModel {
            property bool runActive: false
            property int runTotal: 0
            property int runFinished: 0
            property real runProgress: 0
        }
    }

    Component {
        id: spyComponent

        SignalSpy {}
    }

    Component {
        id: flyoutComponent

        TransferFlyout {}
    }

    // A window too short for the row list's own ceiling, so the panel's clamp is
    // what has to hold.
    Component {
        id: shortParentComponent

        Item {
            width: 600
            height: 180
        }
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

    function addRow(stub, direction, name, state, progress, jobId) {
        stub.append({
                        "direction": direction,
                        "name": name,
                        "state": state,
                        "progress": progress,
                        // Defaulted so the callers that do not care which job a row
                        // names stay five-argument.
                        "jobId": jobId === undefined ? stub.count + 1 : jobId
                    });
    }

    // The bar alone until asked otherwise: an auto-opened flyout must not put a
    // whole panel up unbidden.
    function test_startsMinimised() {
        const stub = createTemporaryObject(stubComponent, testCase);
        const flyout = makeFlyout(stub);

        stub.runActive = true;

        verify(flyout.shown);
        verify(!flyout.expanded);
    }

    // Expanding is deliberate, so it must survive the run settling -- otherwise the
    // panel takes itself away three seconds after you opened it to read.
    function test_expandingTakesItOffTheAutoHide() {
        const stub = createTemporaryObject(stubComponent, testCase);
        const flyout = makeFlyout(stub);

        stub.runTotal = 2;
        stub.runActive = true;
        verify(flyout.autoOpened);

        flyout.toggleExpanded();

        verify(flyout.expanded);
        verify(!flyout.autoOpened);
    }

    // A run starting behind an open panel must not hand it to the auto-hide, or the
    // rows you opened it to read vanish three seconds after they finish.
    function test_aRunDoesNotReclaimAHandOpenedBar() {
        const stub = createTemporaryObject(stubComponent, testCase);
        const flyout = makeFlyout(stub);

        flyout.show();
        stub.runTotal = 1;
        stub.runActive = true;

        verify(flyout.shown);
        verify(!flyout.autoOpened);
    }

    function test_hidingCollapses() {
        const stub = createTemporaryObject(stubComponent, testCase);
        const flyout = makeFlyout(stub);

        flyout.show();
        flyout.toggleExpanded();
        flyout.hide();

        verify(!flyout.shown);
        verify(!flyout.expanded);
    }

    // Finished rows are the point of keeping the model's history: the panel lists
    // them beside whatever is still in flight.
    function test_expandedListsFinishedRowsToo() {
        const stub = createTemporaryObject(stubComponent, testCase);
        const flyout = makeFlyout(stub);

        addRow(stub, TransferDirection.Download, "done.jpg", TransferState.Completed, 1);
        addRow(stub, TransferDirection.Upload, "going.zip", TransferState.Active, 0.4);
        flyout.show();
        flyout.toggleExpanded();

        compare(flyout.rowCount, 2);
        const rowList = findChild(flyout, "transferRowList");
        verify(rowList !== null);
        compare(rowList.count, 2);
        // Height rather than `visible`: an Item's visible is *effective*
        // visibility, and no window is shown behind a TestCase, so it reads false
        // whatever the binding says. A laid-out list is the observable fact.
        waitForRendering(flyout);
        verify(rowList.height > 0);
    }

    // With no rows the list gives way to a line of text; an empty ListView would
    // be an unexplained gap.
    function test_emptyPanelHidesTheList() {
        const stub = createTemporaryObject(stubComponent, testCase);
        const flyout = makeFlyout(stub);

        flyout.show();
        flyout.toggleExpanded();

        compare(flyout.rowCount, 0);
        // Same reason as the test above for measuring rather than reading
        // `visible`: a hidden layout child is never given a height.
        waitForRendering(flyout);
        compare(findChild(flyout, "transferRowList").height, 0);
    }

    // A long queue scrolls inside the panel rather than growing it without bound.
    function test_theRowListIsCappedAndScrolls() {
        const stub = createTemporaryObject(stubComponent, testCase);
        const flyout = makeFlyout(stub);

        for (let i = 0; i < 40; ++i)
            addRow(stub, TransferDirection.Download, "file-" + i + ".bin", TransferState.Queued, 0);
        flyout.show();
        flyout.toggleExpanded();
        waitForRendering(flyout);

        const rowList = findChild(flyout, "transferRowList");
        compare(rowList.height, flyout.listMaxHeight);
        verify(rowList.contentHeight > rowList.height);
        verify(flyout.height < testCase.height);
    }

    // The same queue in a window too short for that ceiling: the panel itself is
    // clamped, so it cannot ride up past the top edge. Capping only the list would
    // leave the chrome around it outside the budget.
    function test_aShortWindowClampsTheWholePanel() {
        const stub = createTemporaryObject(stubComponent, testCase);
        const shortParent = createTemporaryObject(shortParentComponent, testCase);
        const flyout = createTemporaryObject(flyoutComponent, shortParent, {
                                                 "transfers": stub
                                             });

        for (let i = 0; i < 40; ++i)
            addRow(stub, TransferDirection.Download, "file-" + i + ".bin", TransferState.Queued, 0);
        flyout.show();
        flyout.toggleExpanded();
        waitForRendering(flyout);

        verify(flyout.height <= shortParent.height);
        verify(flyout.y >= 0);
        const rowList = findChild(flyout, "transferRowList");
        verify(rowList.height < flyout.listMaxHeight);
        verify(rowList.contentHeight > rowList.height);
    }

    // Cancelling is the host's job -- the flyout only asks. The button is dead
    // while nothing is in flight, so a settled panel cannot fire it.
    function test_cancelAllAsksTheHostOnlyWhileSomethingRuns() {
        const stub = createTemporaryObject(stubComponent, testCase);
        const flyout = makeFlyout(stub);
        const spy = createTemporaryObject(spyComponent, testCase, {
                                              "target": flyout,
                                              "signalName": "cancelAllRequested"
                                          });

        flyout.show();
        flyout.toggleExpanded();
        const button = findChild(flyout, "cancelAllButton");
        verify(button !== null);
        verify(!button.enabled);

        stub.runTotal = 3;
        stub.runActive = true;
        verify(button.enabled);
        button.clicked();

        compare(spy.count, 1);
    }

    // One row's stop has to name its queue as well as its id: the two services
    // number their jobs independently, so 22 means two different transfers.
    function test_aRowCancelNamesItsOwnQueueAndJob() {
        const stub = createTemporaryObject(stubComponent, testCase);
        const flyout = makeFlyout(stub);
        const spy = createTemporaryObject(spyComponent, testCase, {
                                              "target": flyout,
                                              "signalName": "cancelRequested"
                                          });

        addRow(stub, TransferDirection.Download, "a.bin", TransferState.Active, 0.5, 11);
        addRow(stub, TransferDirection.Upload, "b.bin", TransferState.Queued, 0, 22);
        addRow(stub, TransferDirection.Download, "c.bin", TransferState.Completed, 1, 33);
        flyout.show();
        flyout.toggleExpanded();
        waitForRendering(flyout);

        // A settled row has nothing left to stop.
        verify(!findChild(flyout, "cancelRowButton2").visible);

        findChild(flyout, "cancelRowButton1").clicked();
        compare(spy.count, 1);
        compare(spy.signalArguments[0][0], TransferDirection.Upload);
        compare(spy.signalArguments[0][1], 22);
    }

    function test_stateText_data() {
        return [
                    {
                        tag: "queued",
                        state: TransferState.Queued,
                        progress: 0,
                        expected: "Queued"
                    },
                    {
                        tag: "active",
                        state: TransferState.Active,
                        progress: 0.426,
                        expected: "43%"
                    },
                    {
                        tag: "completed",
                        state: TransferState.Completed,
                        progress: 1,
                        expected: "Done"
                    },
                    {
                        tag: "failed",
                        state: TransferState.Failed,
                        progress: 0.2,
                        expected: "Failed"
                    },
                    {
                        tag: "cancelled",
                        state: TransferState.Cancelled,
                        progress: 0.2,
                        expected: "Cancelled"
                    },
                ];
    }

    function test_stateText(data) {
        const stub = createTemporaryObject(stubComponent, testCase);
        const flyout = makeFlyout(stub);

        compare(flyout.stateText(data.state, data.progress), data.expected);
    }
}
