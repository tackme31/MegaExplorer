import QtQuick
import QtTest
import MegaExplorer

// R6-2b. FileViewInput is the keyboard/hover/background-tap layer FileGridView
// and FileTableView share, so a mistake here is a mistake in both at once. Most
// of what it decides is invisible to ui_shot.py: which of two branches a key
// press took, whether a tap stood down for the rename field, whether the arrow
// keys were consumed.
//
// The event objects are plain JS stand-ins. A real QKeyEvent cannot be
// synthesized from QML and matches(StandardKey) is what most branches turn on,
// so the fake supplies its own matches() and the handler bodies are functions
// rather than inline handlers. `accepted` starts as a sentinel string: "the
// handler wrote false" and "no branch touched it" are different facts, and the
// table's Left/Right depends on the second one.
//
// Not covered: the host's `Keys.onPressed: event => viewInput.handleKey(event)`
// attachment and the real Ctrl+C -> StandardKey.Copy mapping (matches() is
// faked here, so this proves the branch order, not the bindings);
// HoverHandler.hovered / point.position and tap delivery, both of which need a
// window and a real pointer; the newFolderRequested relay, which only fires
// from inside the menu; and focus semantics *entirely* -- takeFocus is a
// counter here, and the focus-scope constraint behind it (see the component's
// own comment) is not observable from QML at all. That one is the first item on
// R6-2b's manual checklist.
TestCase {
    id: testCase
    name: "FileViewInput"

    // A real Flickable, not a fake: the `view` property is typed Flickable, and
    // its contentY is what the scroll-rehover wire is attached to.
    Flickable {
        id: flickView
        width: 200
        height: 200
        contentWidth: 200
        contentHeight: 2000
    }

    Component {
        id: inputComponent
        FileViewInput {}
    }

    function makeFixture(params) {
        const p = params === undefined ? ({}) : params;

        const model = {
            "cursor": p.cursor === undefined ? 0 : p.cursor,
            "selected": p.selected === undefined ? [] : p.selected,
            "selectAllCount": 0,
            "clearCount": 0,
            "selectRowCalls": [],
            "moveCursorCalls": []
        };
        model.cursorRow = function () {
            return model.cursor;
        };
        model.selectedEntries = function () {
            return model.selected;
        };
        model.selectAll = function () {
            model.selectAllCount += 1;
        };
        model.clearSelection = function () {
            model.clearCount += 1;
        };
        model.selectRow = function (row, modifiers) {
            model.selectRowCalls.push({
                                          "row": row,
                                          "modifiers": modifiers
                                      });
        };
        model.moveCursor = function (delta, modifiers) {
            model.moveCursorCalls.push({
                                           "delta": delta,
                                           "modifiers": modifiers
                                       });
        };

        const nav = {
            "fileListModel": model,
            "currentHandle": p.currentHandle === undefined ? 7 : p.currentHandle,
            "atRoot": p.atRoot === undefined ? false : p.atRoot
        };

        const mut = {
            "pasteCount": 0,
            "renameCalls": []
        };
        mut.paste = function () {
            mut.pasteCount += 1;
        };
        mut.renameEntry = function (handle, newName) {
            mut.renameCalls.push({
                                     "handle": handle,
                                     "newName": newName
                                 });
        };
        // Read by FolderBackgroundMenu's onAboutToShow.
        mut.canPaste = function () {
            return false;
        };

        const clip = {
            "cutCount": 0,
            "copyCount": 0,
            "last": null
        };
        clip.cut = function (entries, handle, isRoot) {
            clip.cutCount += 1;
            clip.last = {
                "entries": entries,
                "handle": handle,
                "isRoot": isRoot
            };
        };
        clip.copy = function (entries, handle, isRoot) {
            clip.copyCount += 1;
            clip.last = {
                "entries": entries,
                "handle": handle,
                "isRoot": isRoot
            };
        };

        // The four injected callables, as counters. `row` is what the next hit
        // test answers.
        const probe = {
            "row": p.row === undefined ? -1 : p.row,
            "hitCount": 0,
            "lastPos": null,
            "focusCount": 0,
            "revealCount": 0,
            "lastReveal": -1
        };

        const input = createTemporaryObject(inputComponent, testCase, {
                                                "view": flickView,
                                                "navController": nav,
                                                "mutController": mut,
                                                "clipboard": clip,
                                                "rowAtPos": function (pos) {
                                                    probe.hitCount += 1;
                                                    probe.lastPos = pos;
                                                    return probe.row;
                                                },
                                                "takeFocus": function () {
                                                    probe.focusCount += 1;
                                                },
                                                "revealRow": function (row) {
                                                    probe.revealCount += 1;
                                                    probe.lastReveal = row;
                                                },
                                                "arrowColumns": p.arrowColumns === undefined ? 4 :
                                                                                               p.arrowColumns,
                                                "horizontalArrows": p.horizontalArrows
                                                                    === undefined ? true :
                                                                                    p.horizontalArrows
                                            });
        // A required property left out returns null here, and every later
        // failure would then be an opaque null dereference instead of this line.
        verify(input !== null);
        return {
            "input": input,
            "model": model,
            "nav": nav,
            "mut": mut,
            "clip": clip,
            "probe": probe
        };
    }

    // standard is the list of StandardKeys this press should match -- Windows
    // maps Shift+Delete onto StandardKey.Cut, which is the whole reason the
    // branch order in handleKey() matters.
    function makeKey(key, modifiers, standard) {
        const list = standard === undefined ? [] : standard;
        return {
            "key": key,
            "modifiers": modifiers === undefined ? Qt.NoModifier : modifiers,
            "accepted": "untouched",
            "matches": function (std) {
                return list.indexOf(std) !== -1;
            }
        };
    }

    readonly property var oneEntry: [
        {
            "handle": 11,
            "name": "a.txt"
        }
    ]
    readonly property var twoEntries: [
        {
            "handle": 11,
            "name": "a.txt"
        },
        {
            "handle": 12,
            "name": "b.txt"
        }
    ]

    function init() {
        flickView.contentY = 0;
    }

    // ---- handleKey: the guards ---------------------------------------------

    function test_handleKey_standsDownForAlt() {
        const f = makeFixture();
        const ev = testCase.makeKey(Qt.Key_F2, Qt.AltModifier);
        f.input.handleKey(ev);
        compare(ev.accepted, "untouched");
        compare(f.input.renamingHandle, 0);
    }

    // The rename field does not consume F2/Delete, so this layer has to.
    function test_handleKey_standsDownWhileRenaming() {
        const f = makeFixture({
                                  "selected": testCase.oneEntry
                              });
        f.input.renamingHandle = 99;
        const ev = testCase.makeKey(Qt.Key_Delete, Qt.NoModifier, [StandardKey.Cut]);
        f.input.handleKey(ev);
        compare(ev.accepted, "untouched");
        compare(f.clip.cutCount, 0);
    }

    // The most valuable case in this file: StandardKey.Cut is Ctrl+X *and*
    // Shift+Delete on Windows, and the Delete branch tests no modifiers -- so
    // hoisting the clipboard branches above it silently turns "Rubbish bin" into
    // "cut". A non-empty selection is what makes the two outcomes differ.
    function test_handleKey_shiftDeleteIsRubbishBinNotCut() {
        const f = makeFixture({
                                  "selected": testCase.oneEntry
                              });
        const ev = testCase.makeKey(Qt.Key_Delete, Qt.ShiftModifier, [StandardKey.Cut]);
        f.input.handleKey(ev);
        compare(f.clip.cutCount, 0);
        compare(ev.accepted, true);
    }

    function test_handleKey_f2BeginsRename() {
        const f = makeFixture({
                                  "selected": testCase.oneEntry
                              });
        const ev = testCase.makeKey(Qt.Key_F2);
        f.input.handleKey(ev);
        compare(f.input.renamingHandle, 11);
        compare(ev.accepted, true);
    }

    // ---- handleKey: the clipboard / select-all branches ---------------------

    function test_handleKey_standardKeys_data() {
        return [
                    {
                        "tag": "select all",
                        "standard": [StandardKey.SelectAll],
                        "selectAllCount": 1,
                        "copyCount": 0,
                        "cutCount": 0,
                        "pasteCount": 0
                    },
                    {
                        "tag": "copy",
                        "standard": [StandardKey.Copy],
                        "selectAllCount": 0,
                        "copyCount": 1,
                        "cutCount": 0,
                        "pasteCount": 0
                    },
                    {
                        "tag": "cut",
                        "standard": [StandardKey.Cut],
                        "selectAllCount": 0,
                        "copyCount": 0,
                        "cutCount": 1,
                        "pasteCount": 0
                    },
                    {
                        "tag": "paste",
                        "standard": [StandardKey.Paste],
                        "selectAllCount": 0,
                        "copyCount": 0,
                        "cutCount": 0,
                        "pasteCount": 1
                    }
                ];
    }

    function test_handleKey_standardKeys(data) {
        const f = makeFixture({
                                  "selected": testCase.oneEntry
                              });
        const ev = testCase.makeKey(Qt.Key_X, Qt.ControlModifier, data.standard);
        f.input.handleKey(ev);
        compare(f.model.selectAllCount, data.selectAllCount);
        compare(f.clip.copyCount, data.copyCount);
        compare(f.clip.cutCount, data.cutCount);
        compare(f.mut.pasteCount, data.pasteCount);
        compare(ev.accepted, true);
    }

    // ---- handleKey: the arrow matrix ---------------------------------------

    function test_handleKey_arrows_data() {
        return [
                    // Grid: four directions, Up/Down a whole row of tiles.
                    {
                        "tag": "grid up",
                        "arrowColumns": 4,
                        "horizontalArrows": true,
                        "key": Qt.Key_Up,
                        "moved": true,
                        "delta": -4
                    },
                    {
                        "tag": "grid down",
                        "arrowColumns": 4,
                        "horizontalArrows": true,
                        "key": Qt.Key_Down,
                        "moved": true,
                        "delta": 4
                    },
                    {
                        "tag": "grid left",
                        "arrowColumns": 4,
                        "horizontalArrows": true,
                        "key": Qt.Key_Left,
                        "moved": true,
                        "delta": -1
                    },
                    {
                        "tag": "grid right",
                        "arrowColumns": 4,
                        "horizontalArrows": true,
                        "key": Qt.Key_Right,
                        "moved": true,
                        "delta": 1
                    },
                    {
                        "tag": "table up",
                        "arrowColumns": 1,
                        "horizontalArrows": false,
                        "key": Qt.Key_Up,
                        "moved": true,
                        "delta": -1
                    },
                    {
                        "tag": "table down",
                        "arrowColumns": 1,
                        "horizontalArrows": false,
                        "key": Qt.Key_Down,
                        "moved": true,
                        "delta": 1
                    },
                    // The reason horizontalArrows exists: the table passes
                    // Left/Right on untouched rather than treating them as a
                    // one-item move.
                    {
                        "tag": "table left",
                        "arrowColumns": 1,
                        "horizontalArrows": false,
                        "key": Qt.Key_Left,
                        "moved": false,
                        "delta": 0
                    },
                    {
                        "tag": "table right",
                        "arrowColumns": 1,
                        "horizontalArrows": false,
                        "key": Qt.Key_Right,
                        "moved": false,
                        "delta": 0
                    },
                    {
                        "tag": "not a cursor key",
                        "arrowColumns": 4,
                        "horizontalArrows": true,
                        "key": Qt.Key_A,
                        "moved": false,
                        "delta": 0
                    }
                ];
    }

    function test_handleKey_arrows(data) {
        const f = makeFixture({
                                  "arrowColumns": data.arrowColumns,
                                  "horizontalArrows": data.horizontalArrows,
                                  "cursor": 3
                              });
        const ev = testCase.makeKey(data.key, Qt.ShiftModifier);
        f.input.handleKey(ev);

        if (!data.moved) {
            compare(f.model.moveCursorCalls.length, 0);
            compare(f.probe.revealCount, 0);
            compare(ev.accepted, "untouched");
            return;
        }

        compare(f.model.moveCursorCalls.length, 1);
        compare(f.model.moveCursorCalls[0].delta, data.delta);
        // Shift is what turns an arrow into a range extension, so it has to
        // reach the model rather than being swallowed here.
        compare(f.model.moveCursorCalls[0].modifiers, Qt.ShiftModifier);
        compare(f.probe.revealCount, 1);
        compare(f.probe.lastReveal, 3);
        compare(ev.accepted, true);
    }

    // An arrow with nothing to move to still consumes the key; there is just
    // nothing to scroll to.
    function test_handleKey_arrowWithNoCursorDoesNotReveal() {
        const f = makeFixture({
                                  "cursor": -1
                              });
        const ev = testCase.makeKey(Qt.Key_Down);
        f.input.handleKey(ev);
        compare(f.probe.revealCount, 0);
        compare(ev.accepted, true);
    }

    // ---- rename ------------------------------------------------------------

    function test_beginRename_data() {
        return [
                    {
                        "tag": "already renaming",
                        "renaming": 5,
                        "cursor": 0,
                        "selected": "one",
                        "handle": 5,
                        "revealCount": 0
                    },
                    {
                        "tag": "no cursor row",
                        "renaming": 0,
                        "cursor": -1,
                        "selected": "one",
                        "handle": 0,
                        "revealCount": 0
                    },
                    // selectRow(row, NoModifier) collapses the selection first,
                    // so more than one entry left over means the model refused --
                    // renaming is single-item by nature.
                    {
                        "tag": "multiple selected",
                        "renaming": 0,
                        "cursor": 0,
                        "selected": "two",
                        "handle": 0,
                        "revealCount": 0
                    },
                    {
                        "tag": "one selected",
                        "renaming": 0,
                        "cursor": 2,
                        "selected": "one",
                        "handle": 11,
                        "revealCount": 1
                    }
                ];
    }

    function test_beginRename(data) {
        const f = makeFixture({
                                  "cursor": data.cursor,
                                  "selected": data.selected === "two" ? testCase.twoEntries :
                                                                        testCase.oneEntry
                              });
        f.input.renamingHandle = data.renaming;

        f.input.beginRename();

        compare(f.input.renamingHandle, data.handle);
        compare(f.probe.revealCount, data.revealCount);
        if (data.revealCount > 0)
            compare(f.probe.lastReveal, data.cursor);
    }

    // The trap R6-2b was written around: moving this into the component turned
    // `root` from the view into the component, so a plain forceActiveFocus()
    // here would focus the component and leave the view's arrow keys dead.
    function test_endRename_handsFocusBackToTheView() {
        const f = makeFixture();
        f.input.renamingHandle = 11;
        f.input.endRename();
        compare(f.input.renamingHandle, 0);
        compare(f.probe.focusCount, 1);
    }

    function test_commitRename_data() {
        return [
                    {
                        "tag": "changed",
                        "oldName": "a.txt",
                        "newName": "b.txt",
                        "renameCount": 1
                    },
                    {
                        "tag": "unchanged",
                        "oldName": "a.txt",
                        "newName": "a.txt",
                        "renameCount": 0
                    }
                ];
    }

    function test_commitRename(data) {
        const f = makeFixture();
        f.input.renamingHandle = 11;

        f.input.commitRename(11, data.oldName, data.newName);

        compare(f.mut.renameCalls.length, data.renameCount);
        if (data.renameCount > 0) {
            compare(f.mut.renameCalls[0].handle, 11);
            compare(f.mut.renameCalls[0].newName, data.newName);
        }
        // Deferred past the end of the field's own committed handler.
        compare(f.input.renamingHandle, 11);
        tryCompare(f.input, "renamingHandle", 0);
    }

    // ---- clipboard ---------------------------------------------------------

    function test_putOnClipboard_data() {
        return [
                    {
                        "tag": "nothing selected",
                        "empty": true,
                        "cut": true,
                        "cutCount": 0,
                        "copyCount": 0
                    },
                    {
                        "tag": "cut",
                        "empty": false,
                        "cut": true,
                        "cutCount": 1,
                        "copyCount": 0
                    },
                    {
                        "tag": "copy",
                        "empty": false,
                        "cut": false,
                        "cutCount": 0,
                        "copyCount": 1
                    }
                ];
    }

    function test_putOnClipboard(data) {
        const f = makeFixture({
                                  "selected": data.empty ? [] : testCase.twoEntries,
                                  "currentHandle": 55,
                                  "atRoot": true
                              });

        f.input.putOnClipboard(data.cut);

        compare(f.clip.cutCount, data.cutCount);
        compare(f.clip.copyCount, data.copyCount);
        if (data.empty)
            return;
        // The source folder travels with the entries: the paste may happen in
        // another tab, long after this one navigated away.
        compare(f.clip.last.handle, 55);
        compare(f.clip.last.isRoot, true);
        compare(f.clip.last.entries.length, 2);
    }

    // ---- taps --------------------------------------------------------------

    function test_handleLeftTap_data() {
        return [
                    {
                        "tag": "empty space clears",
                        "row": -1,
                        "renaming": 0,
                        "cursor": 0,
                        "focusCount": 1,
                        "clearCount": 1,
                        "selectCount": 0
                    },
                    {
                        "tag": "a row selects",
                        "row": 2,
                        "renaming": 0,
                        "cursor": 0,
                        "focusCount": 1,
                        "clearCount": 0,
                        "selectCount": 1
                    },
                    // The handler has a passive grab only, so it fires for taps
                    // inside the live rename field too -- and taking focus there
                    // would commit the edit on the user's first click into their
                    // own text.
                    {
                        "tag": "inside the rename field",
                        "row": 2,
                        "renaming": 11,
                        "cursor": 2,
                        "focusCount": 0,
                        "clearCount": 0,
                        "selectCount": 0
                    },
                    // Another row while renaming still commits, via focus loss.
                    {
                        "tag": "another row while renaming",
                        "row": 3,
                        "renaming": 11,
                        "cursor": 2,
                        "focusCount": 1,
                        "clearCount": 0,
                        "selectCount": 1
                    }
                ];
    }

    function test_handleLeftTap(data) {
        const f = makeFixture({
                                  "row": data.row,
                                  "cursor": data.cursor
                              });
        f.input.renamingHandle = data.renaming;

        f.input.handleLeftTap(Qt.point(10, 20), Qt.ControlModifier);

        compare(f.probe.focusCount, data.focusCount);
        compare(f.model.clearCount, data.clearCount);
        compare(f.model.selectRowCalls.length, data.selectCount);
        compare(f.probe.lastPos, Qt.point(10, 20));
        if (data.selectCount > 0) {
            compare(f.model.selectRowCalls[0].row, data.row);
            // Ctrl+click toggles rather than replaces, so the modifiers have to
            // reach the model unchanged.
            compare(f.model.selectRowCalls[0].modifiers, Qt.ControlModifier);
        }
    }

    // A delegate's own right-button handler takes a passive grab and therefore
    // fires as well as this one, so a tap that landed on a row must bail out
    // here or the selection menu would be preceded by a clearSelection().
    function test_handleRightTap_standsDownOnARow() {
        const f = makeFixture({
                                  "row": 0
                              });
        f.input.handleRightTap(Qt.point(10, 20));
        compare(f.probe.focusCount, 0);
        compare(f.model.clearCount, 0);
    }

    function test_handleRightTap_onEmptySpaceClearsFirst() {
        const f = makeFixture({
                                  "row": -1
                              });
        f.input.handleRightTap(Qt.point(10, 20));
        compare(f.probe.focusCount, 1);
        compare(f.model.clearCount, 1);
    }

    // ---- hover -------------------------------------------------------------

    function test_resolveHover_data() {
        return [
                    {
                        "tag": "hovering a row",
                        "hovered": true,
                        "row": 4,
                        "hoverRow": 4,
                        "hitCount": 1
                    },
                    {
                        "tag": "hovering the gap",
                        "hovered": true,
                        "row": -1,
                        "hoverRow": -1,
                        "hitCount": 1
                    },
                    // Not hovering is answered without a hit test at all.
                    {
                        "tag": "not hovering",
                        "hovered": false,
                        "row": 4,
                        "hoverRow": -1,
                        "hitCount": 0
                    }
                ];
    }

    function test_resolveHover(data) {
        const f = makeFixture({
                                  "row": data.row
                              });
        f.input.resolveHover(data.hovered, Qt.point(1, 2));
        compare(f.input.hoverRow, data.hoverRow);
        compare(f.probe.hitCount, data.hitCount);
    }

    // Scrolling slides a different row under a stationary pointer and delivers
    // no point event, so the Connections is the only thing that re-resolves it.
    // failOnWarning catches the way that wire dies: a signal name that does not
    // exist warns once at creation and then silently never fires.
    function test_contentY_reResolvesHover() {
        failOnWarning(/Connections/);
        const f = makeFixture({
                                  "row": 4
                              });
        f.input.hoverRow = 4;

        flickView.contentY = 40;

        // hovered is false with no window, so the re-resolve lands on "nothing
        // under the pointer" -- which is still proof the handler ran.
        compare(f.input.hoverRow, -1);
    }
}
