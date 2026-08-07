import QtQuick
import QtTest
import MegaExplorer

// R6-5. NodeDropArea is the one drop target four delegates share, so a mistake
// here is a mistake in all four at once -- and it fails silently, as a target
// that lights up when it should not, or an external drop that vanishes. This
// file is the only safety net R6-1 has when those four inline copies are
// deleted; screenshots cannot see any of it.
//
// A DragEvent cannot be synthesized from QML, so nothing here goes through the
// DropArea signals: the handler bodies are functions on the component and the
// tests call them with plain JS stand-ins for the event. That is also why the
// fakes are what they are -- see makeDrag() and proxyComponent below.
//
// Not covered: the containsDrag -> syncCopyMode wire itself. containsDrag is
// read-only and only ever true inside a live drag session, so with no window it
// is permanently false. Both halves of the guard it feeds are covered; the
// reading of it is not.
TestCase {
    id: testCase
    name: "NodeDropArea"

    Component {
        id: areaComponent
        NodeDropArea {}
    }

    // A QtObject, not a JS object literal like the uploads fake below:
    // Connections.target is typed QObject*, so a plain JS object lands there as
    // null, the copyMode re-ask path goes silently dead and the tests for it
    // pass anyway. A declared `property bool copyMode` gives a real
    // copyModeChanged to attach to.
    Component {
        id: proxyComponent

        QtObject {
            id: fakeProxy

            property bool active: false
            property bool copyMode: false
            property bool verdict: false

            property int canDropCount: 0
            property var lastCanDrop: null
            property int dropOnCount: 0
            property var lastDropOn: null

            function canDropOn(handle, isRoot) {
                fakeProxy.canDropCount += 1;
                fakeProxy.lastCanDrop = {
                    "handle": handle,
                    "isRoot": isRoot
                };
                return fakeProxy.verdict;
            }

            function dropOn(handle, isRoot) {
                fakeProxy.dropOnCount += 1;
                fakeProxy.lastDropOn = {
                    "handle": handle,
                    "isRoot": isRoot
                };
            }
        }
    }

    // uploads is never a Connections target, so the tst_DragProxy.qml trick of a
    // plain JS object with the methods on it is enough.
    function makeUploads(verdict) {
        const rec = {
            "canUploadCount": 0,
            "lastCanUpload": null,
            "dropUrlsCount": 0,
            "lastDropUrls": null
        };
        rec.canUploadTo = function (handle, isRoot) {
            rec.canUploadCount += 1;
            rec.lastCanUpload = {
                "handle": handle,
                "isRoot": isRoot
            };
            return verdict;
        };
        rec.dropUrls = function (urls, handle, isRoot) {
            rec.dropUrlsCount += 1;
            rec.lastDropUrls = {
                "urls": urls,
                "handle": handle,
                "isRoot": isRoot
            };
        };
        return rec;
    }

    // accepted starts as a sentinel rather than false: "the external branch wrote
    // false" and "no branch touched it at all" are different facts, and the move
    // path depends on the second one -- it relies on implicit acceptance by key
    // match, which an assignment of any value would break.
    function makeDrag(hasUrls, urls) {
        const rec = {
            "hasUrls": hasUrls,
            "urls": urls === undefined ? [] : urls,
            "accepted": "untouched",
            "acceptCount": 0,
            "acceptedAction": undefined
        };
        rec.accept = function (action) {
            rec.acceptCount += 1;
            rec.acceptedAction = action;
        };
        return rec;
    }

    function makeProxy(active, verdict) {
        const proxy = createTemporaryObject(proxyComponent, testCase);
        proxy.active = active;
        proxy.verdict = verdict;
        return proxy;
    }

    function makeArea(proxy, uploads, handle, isRoot) {
        const area = createTemporaryObject(areaComponent, testCase, {
                                               "dragProxy": proxy,
                                               "uploads": uploads,
                                               "targetHandle": handle === undefined ? 99 : handle,
                                               "targetIsRoot": isRoot === undefined ? false : isRoot
                                           });
        // A required property left out returns null here, and every later failure
        // would then be an opaque null dereference instead of this line.
        verify(area !== null);
        return area;
    }

    // Losing "text/uri-list" does not break anything visibly -- external drops
    // just stop arriving, because an OS drop is matched against its QMimeData's
    // format strings rather than against Drag.keys.
    function test_keys_areTheTwoMimeTypes() {
        const area = makeArea(makeProxy(false, false), makeUploads(false));
        compare(area.keys, ["application/x-megaexplorer-nodes", "text/uri-list"]);
    }

    function test_accepting_startsFalse() {
        const area = makeArea(makeProxy(false, false), makeUploads(false));
        compare(area.accepting, false);
    }

    function test_evaluateEntry_data() {
        return [
                    {
                        tag: "internal/allowed",
                        active: true,
                        verdict: true,
                        hasUrls: false,
                        uploadVerdict: false,
                        accepting: true,
                        accepted: "untouched",
                        canDropCount: 1,
                        canUploadCount: 0
                    },
                    {
                        tag: "internal/refused",
                        active: true,
                        verdict: false,
                        hasUrls: false,
                        uploadVerdict: true,
                        accepting: false,
                        accepted: "untouched",
                        canDropCount: 1,
                        canUploadCount: 0
                    },
                    // active wins over hasUrls: hasUrls is a claim about the
                    // event, active a claim about the object the internal branch
                    // then dereferences.
                    {
                        tag: "internal/withUrls",
                        active: true,
                        verdict: true,
                        hasUrls: true,
                        uploadVerdict: false,
                        accepting: true,
                        accepted: "untouched",
                        canDropCount: 1,
                        canUploadCount: 0
                    },
                    {
                        tag: "external/allowed",
                        active: false,
                        verdict: true,
                        hasUrls: true,
                        uploadVerdict: true,
                        accepting: true,
                        accepted: true,
                        canDropCount: 0,
                        canUploadCount: 1
                    },
                    {
                        tag: "external/refused",
                        active: false,
                        verdict: true,
                        hasUrls: true,
                        uploadVerdict: false,
                        accepting: false,
                        accepted: false,
                        canDropCount: 0,
                        canUploadCount: 1
                    },
                    {
                        tag: "neither",
                        active: false,
                        verdict: true,
                        hasUrls: false,
                        uploadVerdict: true,
                        accepting: false,
                        accepted: "untouched",
                        canDropCount: 0,
                        canUploadCount: 0
                    }
                ];
    }

    // The accepted column is the "only the external branch touches drag.accepted"
    // invariant; the two count columns are the "only one of the two is even
    // asked" one.
    function test_evaluateEntry(data) {
        const proxy = makeProxy(data.active, data.verdict);
        const uploads = makeUploads(data.uploadVerdict);
        const area = makeArea(proxy, uploads);
        const drag = makeDrag(data.hasUrls);

        area.evaluateEntry(drag);

        compare(area.accepting, data.accepting);
        compare(drag.accepted, data.accepted);
        compare(proxy.canDropCount, data.canDropCount);
        compare(uploads.canUploadCount, data.canUploadCount);
    }

    function test_evaluateEntry_passesTargetThrough_data() {
        return [
                    {
                        tag: "internal/folder",
                        active: true,
                        handle: 99,
                        isRoot: false
                    },
                    // handle 0 is the account root, and it is falsy -- a guard
                    // written as `if (handle)` would drop this one.
                    {
                        tag: "internal/root",
                        active: true,
                        handle: 0,
                        isRoot: true
                    },
                    {
                        tag: "external/folder",
                        active: false,
                        handle: 99,
                        isRoot: false
                    },
                    {
                        tag: "external/root",
                        active: false,
                        handle: 0,
                        isRoot: true
                    }
                ];
    }

    function test_evaluateEntry_passesTargetThrough(data) {
        const proxy = makeProxy(data.active, true);
        const uploads = makeUploads(true);
        const area = makeArea(proxy, uploads, data.handle, data.isRoot);

        area.evaluateEntry(makeDrag(!data.active));

        const asked = data.active ? proxy.lastCanDrop : uploads.lastCanUpload;
        compare(asked.handle, data.handle);
        compare(asked.isRoot, data.isRoot);
    }

    // Ctrl going down while the pointer sits still delivers no drag event at all,
    // so the verdict has to be re-asked off copyMode instead.
    function test_syncCopyMode_reasksWhileHovering() {
        const proxy = makeProxy(true, false);
        const area = makeArea(proxy, makeUploads(false));
        area.evaluateEntry(makeDrag(false));
        compare(area.accepting, false);

        proxy.verdict = true;
        area.syncCopyMode(true);

        compare(area.accepting, true);
        compare(proxy.canDropCount, 2);
    }

    function test_syncCopyMode_ignoredWhenNotHovering() {
        const proxy = makeProxy(true, true);
        const area = makeArea(proxy, makeUploads(false));
        area.evaluateEntry(makeDrag(false));

        proxy.verdict = false;
        area.syncCopyMode(false);

        compare(area.accepting, true);
        compare(proxy.canDropCount, 1);
    }

    // An external drag hovering here has an upload verdict in accepting. Asking
    // the move question would overwrite it with an answer about a drag that is
    // not happening.
    function test_syncCopyMode_ignoredWhenProxyInactive() {
        const proxy = makeProxy(false, false);
        const area = makeArea(proxy, makeUploads(true));
        area.evaluateEntry(makeDrag(true));
        compare(area.accepting, true);

        area.syncCopyMode(true);

        compare(area.accepting, true);
        compare(proxy.canDropCount, 0);
    }

    // The wire itself cannot be observed without a live drag (containsDrag is
    // always false here), but a Connections that failed to attach -- a JS object
    // for a target, a mistyped handler name -- warns, and that much is visible.
    function test_copyModeChanged_isWiredToARealSignal() {
        failOnWarning(/Connections/);
        const proxy = makeProxy(true, true);
        const area = makeArea(proxy, makeUploads(false));

        proxy.copyMode = true;
        proxy.copyMode = false;

        compare(area.accepting, false);
    }

    function test_syncMove_data() {
        return [
                    {
                        tag: "external/accepting",
                        active: false,
                        hasUrls: true,
                        accepting: true,
                        accepted: true
                    },
                    {
                        tag: "external/refusing",
                        active: false,
                        hasUrls: true,
                        accepting: false,
                        accepted: false
                    },
                    {
                        tag: "internal",
                        active: true,
                        hasUrls: false,
                        accepting: true,
                        accepted: "untouched"
                    },
                    {
                        tag: "internal/withUrls",
                        active: true,
                        hasUrls: true,
                        accepting: true,
                        accepted: "untouched"
                    },
                    {
                        tag: "neither",
                        active: false,
                        hasUrls: false,
                        accepting: true,
                        accepted: "untouched"
                    }
                ];
    }

    // Qt drops external drags that stop re-asserting accepted, so this has to run
    // on every move -- but only for them, for the same reason evaluateEntry only
    // assigns it there.
    function test_syncMove(data) {
        const area = makeArea(makeProxy(data.active, true), makeUploads(true));
        area.accepting = data.accepting;
        const drag = makeDrag(data.hasUrls);

        area.syncMove(drag);

        compare(drag.accepted, data.accepted);
    }

    // The target cannot change without leaving first, so a move must not re-ask.
    function test_syncMove_doesNotRecomputeAccepting() {
        const proxy = makeProxy(true, true);
        const uploads = makeUploads(true);
        const area = makeArea(proxy, uploads);
        area.evaluateEntry(makeDrag(false));

        proxy.verdict = false;
        area.syncMove(makeDrag(false));

        compare(area.accepting, true);
        compare(proxy.canDropCount, 1);
        compare(uploads.canUploadCount, 0);
    }

    function test_performDrop_data() {
        return [
                    {
                        tag: "accepting/external",
                        accepting: true,
                        hasUrls: true,
                        acceptCount: 1,
                        dropUrlsCount: 1,
                        dropOnCount: 0
                    },
                    {
                        tag: "accepting/internal",
                        accepting: true,
                        hasUrls: false,
                        acceptCount: 0,
                        dropUrlsCount: 0,
                        dropOnCount: 1
                    },
                    {
                        tag: "refusing/external",
                        accepting: false,
                        hasUrls: true,
                        acceptCount: 0,
                        dropUrlsCount: 0,
                        dropOnCount: 0
                    },
                    {
                        tag: "refusing/internal",
                        accepting: false,
                        hasUrls: false,
                        acceptCount: 0,
                        dropUrlsCount: 0,
                        dropOnCount: 0
                    }
                ];
    }

    function test_performDrop(data) {
        const proxy = makeProxy(false, true);
        const uploads = makeUploads(true);
        const area = makeArea(proxy, uploads);
        area.accepting = data.accepting;
        const drop = makeDrag(data.hasUrls, ["file:///a.txt"]);

        area.performDrop(drop);

        compare(drop.acceptCount, data.acceptCount);
        compare(uploads.dropUrlsCount, data.dropUrlsCount);
        compare(proxy.dropOnCount, data.dropOnCount);
        // Whatever happened, the highlight goes out: the next drag re-asks.
        compare(area.accepting, false);
    }

    // Qt.MoveAction here would make Explorer delete the source file.
    function test_performDrop_acceptsWithCopyAction() {
        const area = makeArea(makeProxy(false, true), makeUploads(true));
        area.accepting = true;
        const drop = makeDrag(true, ["file:///a.txt"]);

        area.performDrop(drop);

        compare(drop.acceptedAction, Qt.CopyAction);
    }

    function test_performDrop_passesUrlsAndTargetThrough() {
        const uploads = makeUploads(true);
        const area = makeArea(makeProxy(false, true), uploads, 0, true);
        area.accepting = true;

        area.performDrop(makeDrag(true, ["file:///a.txt", "file:///b"]));

        compare(uploads.lastDropUrls.urls, ["file:///a.txt", "file:///b"]);
        compare(uploads.lastDropUrls.handle, 0);
        compare(uploads.lastDropUrls.isRoot, true);
    }

    // The asymmetry the hover handlers do not have, and the one thing here most
    // likely to be "tidied" into agreement with them: DragProxy.finish() calls
    // Drag.drop() to deliver this event and Drag.active is cleared as a side
    // effect of that same call, so an internal drop routinely arrives with the
    // proxy already inactive. Re-branching this on active fails right here.
    function test_performDrop_internalPathSurvivesProxyGoingInactive() {
        const proxy = makeProxy(false, true);
        const area = makeArea(proxy, makeUploads(true));
        area.accepting = true;

        area.performDrop(makeDrag(false));

        compare(proxy.dropOnCount, 1);
    }

    function test_performDrop_externalPathIgnoresProxyActive() {
        const proxy = makeProxy(true, true);
        const uploads = makeUploads(true);
        const area = makeArea(proxy, uploads);
        area.accepting = true;

        area.performDrop(makeDrag(true, ["file:///a.txt"]));

        compare(uploads.dropUrlsCount, 1);
        compare(proxy.dropOnCount, 0);
    }

    function test_clearEntry_resetsAccepting() {
        const area = makeArea(makeProxy(true, true), makeUploads(true));
        area.evaluateEntry(makeDrag(false));
        compare(area.accepting, true);

        area.clearEntry();

        compare(area.accepting, false);
    }

    function test_dragEntered_emittedEvenWhenRefused_data() {
        return [
                    {
                        tag: "internal/refused",
                        active: true,
                        verdict: false,
                        hasUrls: false
                    },
                    {
                        tag: "external/refused",
                        active: false,
                        verdict: false,
                        hasUrls: true
                    },
                    {
                        tag: "neither",
                        active: false,
                        verdict: false,
                        hasUrls: false
                    }
                ];
    }

    // TabStrip arms its spring-load clock on this signal and must arm it on a
    // refused target too: this tab's own folder can be a bad destination while a
    // subfolder of it is exactly where the user is heading. Emitting inside the
    // branch instead of after it would break that and nothing else would notice.
    function test_dragEntered_emittedEvenWhenRefused(data) {
        const area = makeArea(makeProxy(data.active, data.verdict), makeUploads(data.verdict));
        let count = 0;
        area.dragEntered.connect(function () {
            count += 1;
        });

        area.evaluateEntry(makeDrag(data.hasUrls));

        compare(area.accepting, false);
        compare(count, 1);
    }

    // FolderTreePanel reads drag.x/drag.y off this to drive edge scrolling, so it
    // has to be the event itself, not a copy of it.
    function test_dragEntered_carriesTheEvent() {
        const area = makeArea(makeProxy(true, true), makeUploads(true));
        const drag = makeDrag(false);
        let seen = null;
        area.dragEntered.connect(function (arg) {
            seen = arg;
        });

        area.evaluateEntry(drag);

        verify(seen === drag);
    }

    // ...and after the verdict, so a handler can read accepting.
    function test_dragEntered_emittedAfterTheVerdict() {
        const area = makeArea(makeProxy(true, true), makeUploads(true));
        let acceptingWhenCalled = false;
        area.dragEntered.connect(function () {
            acceptingWhenCalled = area.accepting;
        });

        area.evaluateEntry(makeDrag(false));

        compare(acceptingWhenCalled, true);
    }

    function test_dragMoved_emittedForEveryBranch_data() {
        return [
                    {
                        tag: "internal",
                        active: true,
                        hasUrls: false
                    },
                    {
                        tag: "external",
                        active: false,
                        hasUrls: true
                    },
                    {
                        tag: "neither",
                        active: false,
                        hasUrls: false
                    }
                ];
    }

    function test_dragMoved_emittedForEveryBranch(data) {
        const area = makeArea(makeProxy(data.active, true), makeUploads(true));
        let count = 0;
        area.dragMoved.connect(function () {
            count += 1;
        });

        area.syncMove(makeDrag(data.hasUrls));

        compare(count, 1);
    }

    function test_dragExited_emitted() {
        const area = makeArea(makeProxy(true, true), makeUploads(true));
        let count = 0;
        area.dragExited.connect(function () {
            count += 1;
        });

        area.clearEntry();

        compare(count, 1);
    }

    // TabStrip stops its spring-load clock here, and a drop it refuses still ends
    // the drag.
    function test_dragDropped_emittedEvenWhenNotAccepting() {
        const area = makeArea(makeProxy(false, true), makeUploads(true));
        area.accepting = false;
        let count = 0;
        area.dragDropped.connect(function () {
            count += 1;
        });

        area.performDrop(makeDrag(false));

        compare(count, 1);
    }

    // Emitted before the branch, unlike the other three: TabStrip's stop-first is
    // a deliberate, commented choice, and the ordering is only visible from
    // inside the handler.
    function test_dragDropped_emittedBeforeTheDrop() {
        const proxy = makeProxy(false, true);
        const area = makeArea(proxy, makeUploads(true));
        area.accepting = true;
        let dropOnCountWhenCalled = -1;
        let acceptingWhenCalled = false;
        area.dragDropped.connect(function () {
            dropOnCountWhenCalled = proxy.dropOnCount;
            acceptingWhenCalled = area.accepting;
        });

        area.performDrop(makeDrag(false));

        compare(dropOnCountWhenCalled, 0);
        compare(acceptingWhenCalled, true);
    }

    function test_enterThenDrop_data() {
        return [
                    {
                        tag: "internal",
                        active: true,
                        hasUrls: false,
                        dropOnCount: 1,
                        dropUrlsCount: 0
                    },
                    {
                        tag: "external",
                        active: false,
                        hasUrls: true,
                        dropOnCount: 0,
                        dropUrlsCount: 1
                    }
                ];
    }

    // accepting is the only thing carried from the hover to the drop.
    function test_enterThenDrop(data) {
        const proxy = makeProxy(data.active, true);
        const uploads = makeUploads(true);
        const area = makeArea(proxy, uploads);

        area.evaluateEntry(makeDrag(data.hasUrls));
        area.performDrop(makeDrag(data.hasUrls, ["file:///a.txt"]));

        compare(proxy.dropOnCount, data.dropOnCount);
        compare(uploads.dropUrlsCount, data.dropUrlsCount);
    }

    // A drop can still arrive after the pointer has left -- Qt delivers it to
    // whoever is under the cursor, and accepting must not survive the exit.
    function test_enterThenExitThenDrop_dropsNothing() {
        const proxy = makeProxy(true, true);
        const uploads = makeUploads(true);
        const area = makeArea(proxy, uploads);

        area.evaluateEntry(makeDrag(false));
        area.clearEntry();
        area.performDrop(makeDrag(false));

        compare(proxy.dropOnCount, 0);
        compare(uploads.dropUrlsCount, 0);
    }
}
