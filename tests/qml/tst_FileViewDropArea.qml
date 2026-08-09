import QtQuick
import QtTest
import MegaExplorer

// R6-2a. FileViewDropArea is the drop target FileGridView and FileTableView
// share, so a mistake here is a mistake in both at once -- and it fails
// silently, as a target that lights up when it should not, or an external drop
// that vanishes. Screenshots cannot see any of it: the highlight only exists
// mid-drag, and ui_shot.py captures a still window.
//
// A DragEvent cannot be synthesized from QML, so nothing here goes through the
// DropArea signals: the handler bodies are functions on the component and the
// tests call them with plain JS stand-ins for the event. See makeDrag() and
// proxyComponent below for what that forces.
//
// Not covered: the containsDrag -> syncCopyMode wire itself (read-only, and
// permanently false with no window -- both halves of the guard it feeds are
// covered, the reading of it is not); that `parent: view` + anchors.fill
// actually puts this over the viewport rather than inside the scrolling
// contentItem; and the viewport outline Rectangle, which has no id to reach.
TestCase {
    id: testCase
    name: "FileViewDropArea"

    // A real Flickable, not a fake: the `view` property is typed Flickable, and
    // it is what makes the edge auto-scrolling observable at all.
    Flickable {
        id: flickView
        width: 200
        height: 200
        contentWidth: 200
        contentHeight: 2000
    }

    Component {
        id: areaComponent
        FileViewDropArea {}
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

            // sourceMutations, not active: this view is where the gesture
            // starts, so active still reads false during its own DragEnter.
            property bool sourceMutations: false
            property bool copyMode: false
            property bool verdict: false

            property int canDropCount: 0
            property var lastCanDrop: null
            property int dropOnCount: 0
            property var lastDropOn: null
            property int beginCount: 0
            property var lastBegin: null

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

            function begin(mutations, entries, label, scenePos, sourceKind) {
                fakeProxy.beginCount += 1;
                fakeProxy.lastBegin = {
                    "mutations": mutations,
                    "entries": entries,
                    "label": label,
                    "scenePos": scenePos,
                    "sourceKind": sourceKind
                };
            }
        }
    }

    // uploads is never a Connections target, so a plain JS object with the
    // methods on it is enough.
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

    function makeNav(entries, currentHandle, atRoot, viewKind) {
        const model = {
            "entryAtCount": 0,
            "selected": []
        };
        model.entryAt = function (row) {
            model.entryAtCount += 1;
            return entries[row];
        };
        model.selectedEntries = function () {
            return model.selected;
        };
        return {
            "fileListModel": model,
            "currentHandle": currentHandle === undefined ? 7 : currentHandle,
            "atRoot": atRoot === undefined ? false : atRoot,
            "viewKind": viewKind === undefined ? ViewKind.CloudDrive : viewKind
        };
    }

    // The host's hit test, as a controllable stand-in. `row` is what the next
    // call returns; count/lastPos are how the tests see that it was consulted
    // and with what.
    function makeHits(row) {
        return {
            "row": row === undefined ? -1 : row,
            "count": 0,
            "lastPos": null
        };
    }

    // accepted starts as a sentinel rather than false: "the external branch
    // wrote false" and "no branch touched it at all" are different facts, and
    // the move path depends on the second one -- it relies on implicit
    // acceptance by key match, which an assignment of any value would break.
    function makeDrag(hasUrls, x, y, urls) {
        const rec = {
            "hasUrls": hasUrls,
            "x": x === undefined ? 100 : x,
            "y": y === undefined ? 100 : y,
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

    function makeProxy(sourceMutations, verdict) {
        const proxy = createTemporaryObject(proxyComponent, testCase);
        proxy.sourceMutations = sourceMutations;
        proxy.verdict = verdict;
        return proxy;
    }

    function makeArea(proxy, uploads, nav, hits) {
        const area = createTemporaryObject(areaComponent, testCase, {
                                               "view": flickView,
                                               "navController": nav,
                                               "mutController": testCase.fakeMutations,
                                               "dragProxy": proxy,
                                               "uploads": uploads,
                                               "rowAtPos": function (pos) {
                                                   hits.count += 1;
                                                   hits.lastPos = pos;
                                                   return hits.row;
                                               }
                                           });
        // A required property left out returns null here, and every later
        // failure would then be an opaque null dereference instead of this line.
        verify(area !== null);
        return area;
    }

    readonly property var fakeMutations: ({
                                              "marker": "mutations"
                                          })

    readonly property var folderRow: [
        {
            "isFolder": true,
            "handle": 42
        }
    ]
    readonly property var fileRow: [
        {
            "isFolder": false,
            "handle": 43
        }
    ]

    function init() {
        // The Flickable is shared, and the auto-scroll tests move it.
        flickView.contentY = 0;
    }

    // Losing "text/uri-list" does not break anything visibly -- external drops
    // just stop arriving, because an OS drop is matched against its QMimeData's
    // format strings rather than against Drag.keys.
    function test_keys_areTheTwoMimeTypes() {
        const area = makeArea(makeProxy(false, false), makeUploads(false), makeNav([]), makeHits());
        compare(area.keys, ["application/x-megaexplorer-nodes", "text/uri-list"]);
    }

    function test_dropTarget_startsCleared() {
        const area = makeArea(makeProxy(false, false), makeUploads(false), makeNav([]), makeHits());
        compare(area.dropRow, -1);
        compare(area.dropOnCurrentFolder, false);
        // The table passes nothing; only the grid's outline is rounded.
        compare(area.outlineRadius, 0);
    }

    function test_updateDropTarget_data() {
        return [
                    // Internal move onto a folder that accepts it: the row wins,
                    // and nothing writes drag.accepted.
                    {
                        "tag": "move onto an accepting folder",
                        "hasUrls": false,
                        "sourceMutations": true,
                        "entries": testCase.folderRow,
                        "row": 0,
                        "proxyVerdict": true,
                        "uploadVerdict": false,
                        "dropRow": 0,
                        "onCurrent": false,
                        "accepted": "untouched",
                        "canDropCount": 1,
                        "canUploadCount": 0
                    },
                    // Refused by the folder: falls back to "into the folder being
                    // shown", which is asked separately -- hence two calls.
                    {
                        "tag": "move onto a refusing folder",
                        "hasUrls": false,
                        "sourceMutations": true,
                        "entries": testCase.folderRow,
                        "row": 0,
                        "proxyVerdict": false,
                        "uploadVerdict": false,
                        "dropRow": -1,
                        "onCurrent": false,
                        "accepted": "untouched",
                        "canDropCount": 2,
                        "canUploadCount": 0
                    },
                    {
                        "tag": "move onto a file row",
                        "hasUrls": false,
                        "sourceMutations": true,
                        "entries": testCase.fileRow,
                        "row": 0,
                        "proxyVerdict": true,
                        "uploadVerdict": false,
                        "dropRow": -1,
                        "onCurrent": true,
                        "accepted": "untouched",
                        "canDropCount": 1,
                        "canUploadCount": 0
                    },
                    {
                        "tag": "move onto empty space",
                        "hasUrls": false,
                        "sourceMutations": true,
                        "entries": [],
                        "row": -1,
                        "proxyVerdict": true,
                        "uploadVerdict": false,
                        "dropRow": -1,
                        "onCurrent": true,
                        "accepted": "untouched",
                        "canDropCount": 1,
                        "canUploadCount": 0
                    },
                    {
                        "tag": "upload onto an accepting folder",
                        "hasUrls": true,
                        "sourceMutations": false,
                        "entries": testCase.folderRow,
                        "row": 0,
                        "proxyVerdict": false,
                        "uploadVerdict": true,
                        "dropRow": 0,
                        "onCurrent": false,
                        "accepted": true,
                        "canDropCount": 0,
                        "canUploadCount": 1
                    },
                    {
                        "tag": "upload onto empty space",
                        "hasUrls": true,
                        "sourceMutations": false,
                        "entries": [],
                        "row": -1,
                        "proxyVerdict": false,
                        "uploadVerdict": true,
                        "dropRow": -1,
                        "onCurrent": true,
                        "accepted": true,
                        "canDropCount": 0,
                        "canUploadCount": 1
                    },
                    {
                        "tag": "upload the current folder refuses",
                        "hasUrls": true,
                        "sourceMutations": false,
                        "entries": [],
                        "row": -1,
                        "proxyVerdict": false,
                        "uploadVerdict": false,
                        "dropRow": -1,
                        "onCurrent": false,
                        "accepted": false,
                        "canDropCount": 0,
                        "canUploadCount": 1
                    },
                    // An internal drag that is not ours (no urls, no payload):
                    // rejected outright, and neither collaborator is consulted.
                    {
                        "tag": "a foreign internal drag",
                        "hasUrls": false,
                        "sourceMutations": false,
                        "entries": testCase.folderRow,
                        "row": 0,
                        "proxyVerdict": true,
                        "uploadVerdict": true,
                        "dropRow": -1,
                        "onCurrent": false,
                        "accepted": false,
                        "canDropCount": 0,
                        "canUploadCount": 0
                    }
                ];
    }

    function test_updateDropTarget(data) {
        const proxy = makeProxy(data.sourceMutations, data.proxyVerdict);
        const uploads = makeUploads(data.uploadVerdict);
        const area = makeArea(proxy, uploads, makeNav(data.entries), makeHits(data.row));
        const drag = makeDrag(data.hasUrls);

        area.updateDropTarget(drag);

        compare(area.dropRow, data.dropRow);
        compare(area.dropOnCurrentFolder, data.onCurrent);
        compare(drag.accepted, data.accepted);
        // Which collaborator was asked is the branch itself. Asserting only on
        // the outcome would let the move path answer with the upload verdict.
        compare(proxy.canDropCount, data.canDropCount);
        compare(uploads.canUploadCount, data.canUploadCount);
    }

    // The fallback target is the folder being shown, root-ness included -- not a
    // hardcoded false.
    function test_updateDropTarget_asksAboutTheCurrentFolder() {
        const proxy = makeProxy(true, false);
        const area = makeArea(proxy, makeUploads(false), makeNav([], 55, true), makeHits(-1));
        area.updateDropTarget(makeDrag(false));
        compare(proxy.lastCanDrop.handle, 55);
        compare(proxy.lastCanDrop.isRoot, true);
    }

    // A favourites listing is a query, not a folder -- its currentHandle is 0 -- so
    // the "nothing under the pointer -> into the folder being shown" fallback has
    // no target and must not even be asked about. Its folder rows are real nodes
    // and stay ordinary drop targets, move and upload included (spec 4.3).
    function test_updateDropTarget_favouritesHasNoCurrentFolderFallback_data() {
        return [
                    {
                        "tag": "move onto empty space",
                        "hasUrls": false,
                        "entries": [],
                        "row": -1,
                        "dropRow": -1,
                        "onCurrent": false,
                        "accepted": "untouched",
                        "canDropCount": 0,
                        "canUploadCount": 0
                    },
                    {
                        "tag": "move onto a folder row",
                        "hasUrls": false,
                        "entries": testCase.folderRow,
                        "row": 0,
                        "dropRow": 0,
                        "onCurrent": false,
                        "accepted": "untouched",
                        "canDropCount": 1,
                        "canUploadCount": 0
                    },
                    {
                        "tag": "upload onto empty space",
                        "hasUrls": true,
                        "entries": [],
                        "row": -1,
                        "dropRow": -1,
                        "onCurrent": false,
                        "accepted": false,
                        "canDropCount": 0,
                        "canUploadCount": 0
                    },
                    {
                        "tag": "upload onto a folder row",
                        "hasUrls": true,
                        "entries": testCase.folderRow,
                        "row": 0,
                        "dropRow": 0,
                        "onCurrent": false,
                        "accepted": true,
                        "canDropCount": 0,
                        "canUploadCount": 1
                    }
                ];
    }

    function test_updateDropTarget_favouritesHasNoCurrentFolderFallback(data) {
        const proxy = makeProxy(!data.hasUrls, true);
        const uploads = makeUploads(true);
        const nav = makeNav(data.entries, 0, false, ViewKind.Favourites);
        const area = makeArea(proxy, uploads, nav, makeHits(data.row));
        const drag = makeDrag(data.hasUrls);

        area.updateDropTarget(drag);

        compare(area.dropRow, data.dropRow);
        compare(area.dropOnCurrentFolder, data.onCurrent);
        compare(drag.accepted, data.accepted);
        compare(proxy.canDropCount, data.canDropCount);
        compare(uploads.canUploadCount, data.canUploadCount);
    }

    function test_updateDropTarget_hitTestsAtTheEventPosition() {
        const hits = makeHits(-1);
        const area = makeArea(makeProxy(true, false), makeUploads(false), makeNav([]), hits);
        area.updateDropTarget(makeDrag(false, 30, 70));
        compare(hits.lastPos, Qt.point(30, 70));
        compare(area.lastDragPos, Qt.point(30, 70));
    }

    // Ctrl toggling copy/move mid-hover delivers no drag event, so the internal
    // branch has to be re-runnable from the recorded position alone.
    function test_updateNodeDropTarget_reResolvesWithoutAnEvent() {
        const hits = makeHits(0);
        const area = makeArea(makeProxy(true, true), makeUploads(false), makeNav(testCase.folderRow),
                              hits);
        area.updateDropTarget(makeDrag(false, 30, 70));
        compare(area.dropRow, 0);

        hits.row = -1;
        area.updateNodeDropTarget();

        compare(area.dropRow, -1);
        compare(hits.lastPos, Qt.point(30, 70));
    }

    function test_syncCopyMode_data() {
        return [
                    {
                        "tag": "hovering, ours",
                        "hovering": true,
                        "sourceMutations": true,
                        "hitCount": 1
                    },
                    {
                        "tag": "not hovering",
                        "hovering": false,
                        "sourceMutations": true,
                        "hitCount": 0
                    },
                    // Ctrl during an external drag changes nothing here: the
                    // upload verdict does not depend on the modifier.
                    {
                        "tag": "hovering, not ours",
                        "hovering": true,
                        "sourceMutations": false,
                        "hitCount": 0
                    }
                ];
    }

    function test_syncCopyMode(data) {
        const hits = makeHits(-1);
        const area = makeArea(makeProxy(data.sourceMutations, false), makeUploads(false), makeNav([]),
                              hits);
        area.syncCopyMode(data.hovering);
        compare(hits.count, data.hitCount);
    }

    // A Connections whose signal does not exist warns and does nothing; without
    // this the copyMode path could rot into a no-op unnoticed.
    function test_copyModeChanged_isWiredToARealSignal() {
        failOnWarning(/Connections/);
        const proxy = makeProxy(true, false);
        const area = makeArea(proxy, makeUploads(false), makeNav([]), makeHits(-1));
        proxy.copyMode = !proxy.copyMode;
    }

    function test_performDrop_data() {
        return [
                    {
                        "tag": "move onto a row",
                        "hasUrls": false,
                        "dropRow": 0,
                        "onCurrent": false,
                        "dropOnCount": 1,
                        "dropUrlsCount": 0,
                        "acceptCount": 0
                    },
                    {
                        "tag": "move onto the current folder",
                        "hasUrls": false,
                        "dropRow": -1,
                        "onCurrent": true,
                        "dropOnCount": 1,
                        "dropUrlsCount": 0,
                        "acceptCount": 0
                    },
                    {
                        "tag": "upload onto a row",
                        "hasUrls": true,
                        "dropRow": 0,
                        "onCurrent": false,
                        "dropOnCount": 0,
                        "dropUrlsCount": 1,
                        "acceptCount": 1
                    },
                    // Nothing was lit, so nothing happens -- and in particular
                    // the drop is not accepted on the way past.
                    {
                        "tag": "refused",
                        "hasUrls": true,
                        "dropRow": -1,
                        "onCurrent": false,
                        "dropOnCount": 0,
                        "dropUrlsCount": 0,
                        "acceptCount": 0
                    }
                ];
    }

    function test_performDrop(data) {
        const proxy = makeProxy(true, true);
        const uploads = makeUploads(true);
        const area = makeArea(proxy, uploads, makeNav(testCase.folderRow), makeHits());
        area.dropRow = data.dropRow;
        area.dropOnCurrentFolder = data.onCurrent;
        const drop = makeDrag(data.hasUrls);

        area.performDrop(drop);

        compare(proxy.dropOnCount, data.dropOnCount);
        compare(uploads.dropUrlsCount, data.dropUrlsCount);
        compare(drop.acceptCount, data.acceptCount);
        // Leaving these set would light the next drag before it is resolved.
        compare(area.dropRow, -1);
        compare(area.dropOnCurrentFolder, false);
    }

    // Qt.MoveAction here would tell Explorer the source file was consumed, and
    // it would delete it.
    function test_performDrop_acceptsWithCopyAction() {
        const area = makeArea(makeProxy(false, false), makeUploads(true), makeNav(
                                  testCase.folderRow), makeHits());
        area.dropOnCurrentFolder = true;
        const drop = makeDrag(true);
        area.performDrop(drop);
        compare(drop.acceptedAction, Qt.CopyAction);
    }

    function test_performDrop_targetIsTheHoveredRow() {
        const proxy = makeProxy(true, true);
        const uploads = makeUploads(true);
        const area = makeArea(proxy, uploads, makeNav(testCase.folderRow, 7, true), makeHits());

        area.dropRow = 0;
        area.performDrop(makeDrag(false));
        compare(proxy.lastDropOn.handle, 42);
        // A row is never the root, whatever the view's own folder is.
        compare(proxy.lastDropOn.isRoot, false);

        area.dropOnCurrentFolder = true;
        area.performDrop(makeDrag(true, 0, 0, ["file:///a.txt"]));
        compare(uploads.lastDropUrls.handle, 7);
        compare(uploads.lastDropUrls.isRoot, true);
        compare(uploads.lastDropUrls.urls, ["file:///a.txt"]);
    }

    function test_beginDrag_data() {
        return [
                    {
                        "tag": "nothing selected",
                        "entries": [],
                        "beginCount": 0,
                        "label": ""
                    },
                    {
                        "tag": "one entry",
                        "entries": [
                            {
                                "name": "a.txt",
                                "handle": 1
                            }
                        ],
                        "beginCount": 1,
                        "label": "a.txt"
                    },
                    {
                        "tag": "several entries",
                        "entries": [
                            {
                                "name": "a.txt",
                                "handle": 1
                            },
                            {
                                "name": "b.txt",
                                "handle": 2
                            }
                        ],
                        "beginCount": 1,
                        "label": "2 items"
                    }
                ];
    }

    function test_beginDrag(data) {
        const proxy = makeProxy(false, false);
        const nav = makeNav([]);
        nav.fileListModel.selected = data.entries;
        const area = makeArea(proxy, makeUploads(false), nav, makeHits());

        area.beginDrag(Qt.point(5, 6));

        compare(proxy.beginCount, data.beginCount);
        if (data.beginCount === 0)
            return;
        compare(proxy.lastBegin.label, data.label);
        compare(proxy.lastBegin.scenePos, Qt.point(5, 6));
        // The payload's owner: the drag has to be attributed to this tab's
        // mutation controller, not the window's.
        compare(proxy.lastBegin.mutations, testCase.fakeMutations);
    }

    // Which screen the nodes came from is not recoverable from the payload once the
    // gesture is under way, so the view's own kind travels with it from here.
    function test_beginDrag_carriesTheViewKind() {
        const proxy = makeProxy(false, false);
        const nav = makeNav([], 0, false, ViewKind.Favourites);
        nav.fileListModel.selected = [
                    {
                        "name": "a.txt",
                        "handle": 1
                    }
                ];
        const area = makeArea(proxy, makeUploads(false), nav, makeHits());

        area.beginDrag(Qt.point(5, 6));

        compare(proxy.lastBegin.sourceKind, ViewKind.Favourites);
    }

    // The only test that exercises DragAutoScroller anywhere in the repo. y is
    // inside the bottom margin (viewport 200, margin 24).
    function test_trackDrag_scrollsAtTheEdge() {
        const area = makeArea(makeProxy(true, false), makeUploads(false), makeNav([]), makeHits(
                                  -1));
        area.trackDrag(makeDrag(false, 100, 190));
        tryVerify(() => flickView.contentY > 0);
    }

    function test_trackDrag_doesNotScrollInTheMiddle() {
        const area = makeArea(makeProxy(true, false), makeUploads(false), makeNav([]), makeHits(
                                  -1));
        area.trackDrag(makeDrag(false, 100, 100));
        wait(80);
        compare(flickView.contentY, 0);
    }

    function test_releaseDrag_stopsScrollingAndClears() {
        const area = makeArea(makeProxy(true, false), makeUploads(false), makeNav([]), makeHits(
                                  -1));
        area.dropOnCurrentFolder = true;
        area.trackDrag(makeDrag(false, 100, 190));
        tryVerify(() => flickView.contentY > 0);

        area.releaseDrag();
        const stopped = flickView.contentY;
        wait(80);

        compare(flickView.contentY, stopped);
        compare(area.dropOnCurrentFolder, false);
        compare(area.dropRow, -1);
    }
}
