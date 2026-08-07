import QtQuick
import QtTest
import MegaExplorer

// R4-5. canDropOn is the one function six DropAreas share instead of each
// branching on the drag mode themselves, so a mistake here is a mistake in all
// six at once -- and it fails silently, as a drop target that lights up when it
// should not or stays dead when it should not.
//
// It reads only this object's own properties, so no scene, no singleton and no
// C++ instance is involved. sourceMutations is a `var`, so a plain JS object
// with the two methods on it stands in for the FileMutationController.
//
// Not covered: sampleCopyMode(), which reads live OS modifier keys through
// KeyboardState and cannot be made deterministic from QML. begin(), moveTo()
// and finish() are avoided for the same reason -- each calls it.
TestCase {
    id: testCase
    name: "DragProxy"

    Component {
        id: proxyComponent
        DragProxy {}
    }

    property var lastCall: null

    function makeMutations(answer) {
        return {
            "canCopyEntriesOn": function (entries, handle, isRoot) {
                testCase.lastCall = {
                    "method": "canCopyEntriesOn",
                    "arg": entries,
                    "handle": handle,
                    "isRoot": isRoot
                };
                return answer;
            },
            "canDropHandlesOn": function (handles, handle, isRoot) {
                testCase.lastCall = {
                    "method": "canDropHandlesOn",
                    "arg": handles,
                    "handle": handle,
                    "isRoot": isRoot
                };
                return answer;
            }
        };
    }

    function makeProxy() {
        const proxy = createTemporaryObject(proxyComponent, testCase);
        proxy.entries = [
                    {
                        "handle": 11,
                        "name": "a.txt",
                        "sizeBytes": 1,
                        "isFolder": false
                    },
                    {
                        "handle": 22,
                        "name": "b",
                        "sizeBytes": 0,
                        "isFolder": true
                    }
                ];
        return proxy;
    }

    function init() {
        testCase.lastCall = null;
    }

    // A drag that never recorded where it came from cannot be answered, and the
    // guard has to come before sourceMutations is dereferenced.
    function test_canDropOn_withoutSourceNavIsFalse() {
        const proxy = makeProxy();
        compare(proxy.sourceMutations, null);
        compare(proxy.canDropOn(99, false), false);
    }

    // The two modes ask different questions of different arguments. Copy needs
    // the full entries because a copy is sized and named; a move only needs the
    // handles. Swapping them would still run and still return a bool.
    function test_canDropOn_copyModeAsksCanCopyEntriesOn() {
        const proxy = makeProxy();
        proxy.sourceMutations = makeMutations(true);
        proxy.copyMode = true;
        compare(proxy.canDropOn(99, false), true);
        compare(testCase.lastCall.method, "canCopyEntriesOn");
        compare(testCase.lastCall.arg.length, 2);
        compare(testCase.lastCall.arg[0].name, "a.txt");
    }

    function test_canDropOn_moveModeAsksCanDropHandlesOn() {
        const proxy = makeProxy();
        proxy.sourceMutations = makeMutations(true);
        proxy.copyMode = false;
        compare(proxy.canDropOn(99, false), true);
        compare(testCase.lastCall.method, "canDropHandlesOn");
        // handles is entries mapped to .handle, not the entries themselves.
        compare(testCase.lastCall.arg, [11, 22]);
    }

    function test_canDropOn_passesTargetThrough_data() {
        return [
                    {
                        tag: "folder",
                        handle: 99,
                        isRoot: false
                    },
                    {
                        tag: "root",
                        handle: 0,
                        isRoot: true
                    }
                ];
    }

    function test_canDropOn_passesTargetThrough(data) {
        const proxy = makeProxy();
        proxy.sourceMutations = makeMutations(true);
        proxy.canDropOn(data.handle, data.isRoot);
        compare(testCase.lastCall.handle, data.handle);
        compare(testCase.lastCall.isRoot, data.isRoot);
    }

    // The verdict is the controller's, relayed unchanged in both directions.
    function test_canDropOn_relaysVerdict_data() {
        return [
                    {
                        tag: "allowed/copy",
                        answer: true,
                        copyMode: true
                    },
                    {
                        tag: "refused/copy",
                        answer: false,
                        copyMode: true
                    },
                    {
                        tag: "allowed/move",
                        answer: true,
                        copyMode: false
                    },
                    {
                        tag: "refused/move",
                        answer: false,
                        copyMode: false
                    }
                ];
    }

    function test_canDropOn_relaysVerdict(data) {
        const proxy = makeProxy();
        proxy.sourceMutations = makeMutations(data.answer);
        proxy.copyMode = data.copyMode;
        compare(proxy.canDropOn(99, false), data.answer);
    }

    // handles is a binding over entries, so assigning entries is enough to keep
    // the two in step -- including back down to nothing.
    function test_handles_trackEntries() {
        const proxy = makeProxy();
        compare(proxy.handles, [11, 22]);
        proxy.entries = [
                    {
                        "handle": 7,
                        "name": "c",
                        "sizeBytes": 0,
                        "isFolder": true
                    }
                ];
        compare(proxy.handles, [7]);
        proxy.entries = [];
        compare(proxy.handles, []);
    }
}
