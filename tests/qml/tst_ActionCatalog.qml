import QtQuick
import QtTest
import MegaExplorer

// R4-5. ActionCatalog is a pragma Singleton whose every entry is documented as
// "a function of one ctx object", so a fake ctx is the whole test fixture --
// nothing is instantiated and no C++ instance is involved.
//
// Not covered: the trigger lambdas of download / openInNewTab / togglePin /
// cut / copy. Those five name the downloadController / tabsController /
// quickAccessModel / clipboardController context properties directly, and
// main.cpp is the only thing that ever sets those; reaching them from here
// would mean C++ test doubles injected through QUICK_TEST_MAIN_WITH_SETUP.
// That is a gap, not a decision -- see R4-5 in docs/REFACTOR_PLANS.md.
TestCase {
    id: testCase
    name: "ActionCatalog"

    // Every field any entry reads, so one object serves every row. Individual
    // tests override what they care about.
    function fullCtx() {
        return {
            "handle": 42,
            "isRoot": false,
            "name": "folder",
            "pinned": false,
            "favourited": false,
            "canPaste": true,
            "entries": [
                {
                    "handle": 1,
                    "name": "a.txt",
                    "sizeBytes": 10,
                    "isFolder": false
                },
                {
                    "handle": 2,
                    "name": "b.txt",
                    "sizeBytes": 20,
                    "isFolder": false
                }
            ],
            "navController": null,
            "mutations": null
        };
    }

    // ---- label -------------------------------------------------------------

    function test_label_data() {
        return [
                    {
                        tag: "newFolder",
                        id: "newFolder",
                        expected: "New folder"
                    },
                    {
                        tag: "download",
                        id: "download",
                        expected: "Download"
                    },
                    {
                        tag: "openInNewTab",
                        id: "openInNewTab",
                        expected: "Open in new tab"
                    },
                    {
                        tag: "copyLink",
                        id: "copyLink",
                        expected: "Copy link"
                    },
                    {
                        tag: "removeLink",
                        id: "removeLink",
                        expected: "Remove link"
                    },
                    {
                        tag: "toggleFavourite",
                        id: "toggleFavourite",
                        expected: "Add to Favourites"
                    },
                    {
                        tag: "cut",
                        id: "cut",
                        expected: "Cut"
                    },
                    {
                        tag: "copy",
                        id: "copy",
                        expected: "Copy"
                    },
                    {
                        tag: "paste",
                        id: "paste",
                        expected: "Paste"
                    },
                    {
                        tag: "rename",
                        id: "rename",
                        expected: "Rename"
                    },
                    {
                        tag: "moveToRubbish",
                        id: "moveToRubbish",
                        expected: "Move to Rubbish bin"
                    },
                    {
                        tag: "selectAll",
                        id: "selectAll",
                        expected: "Select all"
                    },
                    {
                        tag: "refresh",
                        id: "refresh",
                        expected: "Refresh"
                    }
                ];
    }

    function test_label(data) {
        compare(ActionCatalog.label(data.id, fullCtx()), data.expected);
    }

    // The one label that reads ctx: the pin entry is a toggle, so the wording
    // has to follow the current state or the menu offers to pin what is pinned.
    function test_label_togglePinFollowsPinnedState() {
        const ctx = fullCtx();
        ctx.pinned = false;
        compare(ActionCatalog.label("togglePin", ctx), "Pin to Quick access");
        ctx.pinned = true;
        compare(ActionCatalog.label("togglePin", ctx), "Unpin from Quick access");
    }

    // Same shape as togglePin above, and the same reason it can't be a
    // hardcoded pair: the resolver hands over one action ID for both directions.
    function test_label_toggleFavouriteFollowsFavouritedState() {
        const ctx = fullCtx();
        ctx.favourited = false;
        compare(ActionCatalog.label("toggleFavourite", ctx), "Add to Favourites");
        ctx.favourited = true;
        compare(ActionCatalog.label("toggleFavourite", ctx), "Remove from Favourites");
    }

    // ---- icon --------------------------------------------------------------

    function test_icon_data() {
        return [
                    {
                        tag: "newFolder",
                        id: "newFolder",
                        expected: Theme.glyph.menu.newFolder
                    },
                    {
                        tag: "download",
                        id: "download",
                        expected: Theme.glyph.menu.download
                    },
                    {
                        tag: "openInNewTab",
                        id: "openInNewTab",
                        expected: Theme.glyph.menu.openInNewTab
                    },
                    {
                        tag: "copyLink",
                        id: "copyLink",
                        expected: Theme.glyph.menu.copyLink
                    },
                    {
                        tag: "removeLink",
                        id: "removeLink",
                        expected: Theme.glyph.menu.removeLink
                    },
                    {
                        tag: "toggleFavourite",
                        id: "toggleFavourite",
                        expected: Theme.glyph.menu.toggleFavourite
                    },
                    {
                        tag: "cut",
                        id: "cut",
                        expected: Theme.glyph.menu.cut
                    },
                    {
                        tag: "copy",
                        id: "copy",
                        expected: Theme.glyph.menu.copy
                    },
                    {
                        tag: "paste",
                        id: "paste",
                        expected: Theme.glyph.menu.paste
                    },
                    {
                        tag: "rename",
                        id: "rename",
                        expected: Theme.glyph.menu.rename
                    },
                    {
                        tag: "moveToRubbish",
                        id: "moveToRubbish",
                        expected: Theme.glyph.menu.moveToRubbish
                    },
                    {
                        tag: "selectAll",
                        id: "selectAll",
                        expected: Theme.glyph.menu.selectAll
                    },
                    {
                        tag: "refresh",
                        id: "refresh",
                        expected: Theme.glyph.menu.refresh
                    }
                ];
    }

    function test_icon(data) {
        const icon = ActionCatalog.icon(data.id, fullCtx());
        compare(icon, data.expected);
        // A missing Theme glyph would compare equal above while both are
        // undefined, which would make every row here vacuous.
        verify(icon !== undefined && icon !== "");
    }

    function test_icon_togglePinFollowsPinnedState() {
        const ctx = fullCtx();
        ctx.pinned = false;
        compare(ActionCatalog.icon("togglePin", ctx), Theme.glyph.menu.pin);
        ctx.pinned = true;
        compare(ActionCatalog.icon("togglePin", ctx), Theme.glyph.menu.unpin);
    }

    // The counterpart of togglePin's icon test, asserting the opposite: this
    // one deliberately does *not* follow the state, so a later "make it a pair
    // like togglePin" edit has to delete a test rather than slip through.
    function test_icon_toggleFavouriteIsTheSameGlyphBothWays() {
        const ctx = fullCtx();
        ctx.favourited = false;
        const off = ActionCatalog.icon("toggleFavourite", ctx);
        ctx.favourited = true;
        compare(ActionCatalog.icon("toggleFavourite", ctx), off);
    }

    // ---- isEnabled ---------------------------------------------------------

    // Twelve of the fourteen entries declare no `enabled` lambda and are always on.
    function test_isEnabled_data() {
        return [
                    {
                        tag: "newFolder",
                        id: "newFolder"
                    },
                    {
                        tag: "copyLink",
                        id: "copyLink"
                    },
                    {
                        tag: "removeLink",
                        id: "removeLink"
                    },
                    {
                        tag: "download",
                        id: "download"
                    },
                    {
                        tag: "openInNewTab",
                        id: "openInNewTab"
                    },
                    {
                        tag: "toggleFavourite",
                        id: "toggleFavourite"
                    },
                    {
                        tag: "cut",
                        id: "cut"
                    },
                    {
                        tag: "copy",
                        id: "copy"
                    },
                    {
                        tag: "rename",
                        id: "rename"
                    },
                    {
                        tag: "moveToRubbish",
                        id: "moveToRubbish"
                    },
                    {
                        tag: "selectAll",
                        id: "selectAll"
                    },
                    {
                        tag: "refresh",
                        id: "refresh"
                    }
                ];
    }

    function test_isEnabled(data) {
        compare(ActionCatalog.isEnabled(data.id, fullCtx()), true);
    }

    // Quick access holds folders, and the root is not one you can pin.
    function test_isEnabled_togglePinOffAtRoot() {
        const ctx = fullCtx();
        ctx.isRoot = false;
        compare(ActionCatalog.isEnabled("togglePin", ctx), true);
        ctx.isRoot = true;
        compare(ActionCatalog.isEnabled("togglePin", ctx), false);
    }

    function test_isEnabled_pasteFollowsCanPaste() {
        const ctx = fullCtx();
        ctx.canPaste = true;
        compare(ActionCatalog.isEnabled("paste", ctx), true);
        ctx.canPaste = false;
        compare(ActionCatalog.isEnabled("paste", ctx), false);
    }

    // The `=== true` in isEnabled exists for exactly this: sites that build a
    // ctx without canPaste (FolderPinMenu) would otherwise get undefined, which
    // is not assignable to a bool property and greys nothing out reliably.
    function test_isEnabled_pasteIsFalseNotUndefinedWithoutCanPaste() {
        const enabled = ActionCatalog.isEnabled("paste", {});
        compare(enabled, false);
        verify(enabled !== undefined);
    }

    // ---- the unknown-ID contract ------------------------------------------

    // Deliberately asymmetric, one return value per call site's needs: a menu
    // item binding a label wants undefined so the binding stays unset, an icon
    // string wants "", an enabled bool wants false, and triggering must not
    // throw. Spelled out here because nothing else records the asymmetry.
    function test_unknownId_label() {
        compare(ActionCatalog.label("nosuch", fullCtx()), undefined);
    }

    function test_unknownId_icon() {
        compare(ActionCatalog.icon("nosuch", fullCtx()), "");
    }

    function test_unknownId_isEnabled() {
        compare(ActionCatalog.isEnabled("nosuch", fullCtx()), false);
    }

    function test_unknownId_triggerIsSilentNoOp() {
        ActionCatalog.trigger("nosuch", fullCtx());
        verify(true);
    }

    // ---- trigger: the seven entries that route through ctx ------------------

    function test_trigger_requestCallbacks_data() {
        return [
                    {
                        tag: "newFolder",
                        id: "newFolder",
                        callback: "requestNewFolder"
                    },
                    {
                        tag: "rename",
                        id: "rename",
                        callback: "requestRename"
                    },
                    {
                        tag: "moveToRubbish",
                        id: "moveToRubbish",
                        callback: "requestMoveToRubbish"
                    }
                ];
    }

    function test_trigger_requestCallbacks(data) {
        let calls = 0;
        const ctx = fullCtx();
        ctx[data.callback] = function () {
            ++calls;
        };
        ActionCatalog.trigger(data.id, ctx);
        compare(calls, 1);
    }

    function test_trigger_pasteCallsMutations() {
        let calls = 0;
        const ctx = fullCtx();
        ctx.mutations = {
            "paste": function () {
                ++calls;
            }
        };
        ActionCatalog.trigger("paste", ctx);
        compare(calls, 1);
    }

    // Sends the opposite of what was sampled, never a re-read: the whole point
    // is that the user gets the state the row they clicked promised them.
    function test_trigger_toggleFavouriteSendsTheOppositeOfTheSampledState() {
        let received = [];
        const ctx = fullCtx();
        ctx.mutations = {
            "setEntryFavourite": function (handle, favourite) {
                received.push([handle, favourite]);
            }
        };

        ctx.favourited = false;
        ActionCatalog.trigger("toggleFavourite", ctx);
        ctx.favourited = true;
        ActionCatalog.trigger("toggleFavourite", ctx);

        compare(received.length, 2);
        compare(received[0][0], 42);
        compare(received[0][1], true);
        compare(received[1][1], false);
    }

    // Both link entries hand the primary handle straight to the mutation
    // controller: neither reads a sampled export state, because none exists.
    function test_trigger_linkActionsCallMutationsWithTheHandle() {
        let copied = [];
        let removed = [];
        const ctx = fullCtx();
        ctx.mutations = {
            "copyLinkToClipboard": function (handle) {
                copied.push(handle);
            },
            "removeLink": function (handle) {
                removed.push(handle);
            }
        };

        ActionCatalog.trigger("copyLink", ctx);
        ActionCatalog.trigger("removeLink", ctx);

        compare(copied, [42]);
        compare(removed, [42]);
    }

    function test_trigger_refreshCallsNavController() {
        let calls = 0;
        const ctx = fullCtx();
        ctx.navController = {
            "refresh": function () {
                ++calls;
            }
        };
        ActionCatalog.trigger("refresh", ctx);
        compare(calls, 1);
    }

    // Reaches two levels down, through the controller to its model.
    function test_trigger_selectAllCallsFileListModel() {
        let calls = 0;
        const ctx = fullCtx();
        ctx.navController = {
            "fileListModel": {
                "selectAll": function () {
                    ++calls;
                }
            }
        };
        ActionCatalog.trigger("selectAll", ctx);
        compare(calls, 1);
    }
}
