pragma Singleton
import QtQuick

// The QML half of the right-click-menu design: what each action is called,
// whether it is greyed out, and what it does. The C++ half
// (src/core/MenuActionResolver.h) owns the complementary question -- which
// actions a site offers and in what order -- and hands over stable string IDs.
// Same C++-supplies-structure / QML-supplies-wording split as
// NotificationController and ToastStack.qml.
//
// Execution deliberately lives here rather than in C++: every target below is
// a QML-side object (the downloadController/tabsController/quickAccessModel
// context properties, or a per-view/per-tab Item), so a C++ command object
// would have to call back into QML for all of them.
//
// Every entry is a function of one `ctx` object, which callers build fresh
// immediately before opening a menu (never bound -- see ActionMenu.qml). Its
// shape is uniform across sites:
//
//   handle, isRoot, name  the single primary target (the clicked row, or the
//                         first selected entry, or the folder a view shows)
//   pinned                whether that target is already in Quick access
//   favourited, exported  whether that target is hearted / has a public link
//   entries               every target, as {handle, name, sizeBytes, isFolder,
//                         isFavourite, isExported, modificationTime}
//   request*()            callbacks into the view/tab that opened the menu,
//                         for actions driving an Item no singleton can reach
//                         (the inline rename field, ConfirmRubbishDialog,
//                         NewFolderDialog)
//   navController         the tab's FolderNavigationController -- the only way
//                         a singleton can reach per-tab state at all
//   mutations             the same tab's FileMutationController, i.e. the half
//                         that changes the remote tree (FolderBackground only,
//                         the one site carrying a paste)
//   canPaste              FolderBackground only: whether a paste would do
//                         anything, sampled when the menu opens
//
// Each entry documents which of those it actually reads.
QtObject {
    id: root

    readonly property var entries: ({
                                        // ctx: openable, requestOpen(). Greyed
                                        // rather than hidden for a file no viewer
                                        // shows, so the top row doesn't come and go.
                                        "open": {
                                            "icon": ctx => Theme.glyph.menu.open,
                                            "label": ctx => qsTr("Open"),
                                            "enabled": ctx => ctx.openable === true,
                                            "trigger": ctx => ctx.requestOpen()
                                        },
                                        // ctx: requestOpenAs(kind). Never greyed:
                                        // overriding the extension is the point.
                                        "openAsImage": {
                                            "icon": ctx => Theme.glyph.menu.openAsImage,
                                            "label": ctx => qsTr("Image"),
                                            "group": "openAs",
                                            "trigger": ctx => ctx.requestOpenAs("image")
                                        },
                                        "openAsVideo": {
                                            "icon": ctx => Theme.glyph.menu.openAsVideo,
                                            "label": ctx => qsTr("Video"),
                                            "group": "openAs",
                                            "trigger": ctx => ctx.requestOpenAs("video")
                                        },
                                        "openAsAudio": {
                                            "icon": ctx => Theme.glyph.menu.openAsAudio,
                                            "label": ctx => qsTr("Audio"),
                                            "group": "openAs",
                                            "trigger": ctx => ctx.requestOpenAs("audio")
                                        },
                                        "openAsPdf": {
                                            "icon": ctx => Theme.glyph.menu.openAsPdf,
                                            "label": ctx => qsTr("PDF"),
                                            "group": "openAs",
                                            "trigger": ctx => ctx.requestOpenAs("pdf")
                                        },
                                        "openAsArchive": {
                                            "icon": ctx => Theme.glyph.menu.openAsArchive,
                                            "label": ctx => qsTr("ZIP archive"),
                                            "group": "openAs",
                                            "trigger": ctx => ctx.requestOpenAs("archive")
                                        },
                                        // ctx: handle. Never greyed, like the Open as
                                        // entries above: a browser that cannot play
                                        // the file offers to download it instead.
                                        "openWithBrowser": {
                                            "icon": ctx => Theme.glyph.menu.openWithBrowser,
                                            "label": ctx => qsTr("Browser"),
                                            "group": "openWith",
                                            "trigger": ctx => viewerController.openInBrowser(
                                                          ctx.handle)
                                        },
                                        // ctx: requestNewFolder()
                                        "newFolder": {
                                            "icon": ctx => Theme.glyph.menu.newFolder,
                                            "label": ctx => qsTr("New folder"),
                                            "trigger": ctx => ctx.requestNewFolder()
                                        },
                                        // ctx: entries
                                        "download": {
                                            "icon": ctx => Theme.glyph.menu.download,
                                            "label": ctx => qsTr("Download"),
                                            "trigger": ctx => {
                                                for (let i = 0; i < ctx.entries.length; ++i) {
                                                    downloadController.downloadFile(
                                                                ctx.entries[i].handle,
                                                                ctx.entries[i].name,
                                                                ctx.entries[i].sizeBytes);
                                                }
                                            }
                                        },
                                        // ctx: handle, localFolderLinked
                                        "openLocalFile": {
                                            "icon": ctx => Theme.glyph.menu.openLocalFile,
                                            "label": ctx => qsTr("Open file"),
                                            // Hidden without a linked folder, for
                                            // the same reason as openLocalLocation
                                            // below.
                                            "available": ctx => ctx.localFolderLinked === true,
                                            "group": "localPath",
                                            "trigger": ctx => localFolderController.openFile(
                                                          ctx.handle)
                                        },
                                        // ctx: handle, localFolderLinked
                                        "openLocalLocation": {
                                            "icon": ctx => Theme.glyph.menu.openLocalLocation,
                                            // "Show", not "Open": explorer.exe is
                                            // asked to select the item, not to open
                                            // it (LocalFolderController.cpp).
                                            "label": ctx => qsTr("Show in Explorer"),
                                            // Hidden rather than greyed, unlike
                                            // paste below: greying advertises a
                                            // feature to everyone who never linked
                                            // a folder, and the C++ resolver has no
                                            // axis for an app-wide setting.
                                            "available": ctx => ctx.localFolderLinked === true,
                                            "group": "localPath",
                                            "trigger": ctx => localFolderController.openLocation(
                                                          ctx.handle)
                                        },
                                        // ctx: handle, isRoot
                                        "openInNewTab": {
                                            "icon": ctx => Theme.glyph.menu.openInNewTab,
                                            "label": ctx => qsTr("Open in new tab"),
                                            // Background tab, current tab keeps focus -- same
                                            // convention as the views' and the tree's middle-click.
                                            "trigger": ctx => tabsController.addTabAt(ctx.handle,
                                                                                      ctx.isRoot)
                                        },
                                        // ctx: handle, isRoot, name, pinned
                                        "togglePin": {
                                            "icon": ctx => ctx.pinned ? Theme.glyph.menu.unpin :
                                                                        Theme.glyph.menu.pin,
                                            "label": ctx => ctx.pinned ? qsTr(
                                                                             "Unpin from Quick access") :
                                                                         qsTr("Pin to Quick access"),
                                            // The Cloud Drive root is permanently the tree's own
                                            // top row, so pinning it could only duplicate it --
                                            // Explorer doesn't allow it either. Greyed rather than
                                            // hidden, which is why applicability alone (C++)
                                            // can't express it.
                                            "enabled": ctx => !ctx.isRoot,
                                            "trigger": ctx => {
                                                if (ctx.pinned)
                                                    quickAccessModel.unpin(ctx.handle);
                                                else
                                                    quickAccessModel.pin(ctx.handle, ctx.name);
                                            }
                                        },
                                        // ctx: handle, favourited, mutations
                                        "toggleFavourite": {
                                            // One glyph either way, unlike togglePin -- see
                                            // Theme.glyph.menu.toggleFavourite for why.
                                            "icon": ctx => Theme.glyph.menu.toggleFavourite,
                                            "label": ctx => ctx.favourited ? qsTr(
                                                                                 "Remove from Favourites") :
                                                                             qsTr("Add to Favourites"),
                                            // No pre-check against ctx.favourited: it was sampled
                                            // when the menu opened, so if it has drifted from the
                                            // server the user still gets the state the row they
                                            // read promised them. The attribute write is
                                            // idempotent (IMegaClient::setNodeFavourite).
                                            "trigger": ctx => ctx.mutations.setEntryFavourite(
                                                                  ctx.handle, !ctx.favourited)
                                        },
                                        // ctx: handle, mutations
                                        "copyLink": {
                                            "icon": ctx => Theme.glyph.menu.copyLink,
                                            // "Copy" rather than "Get": the link ends
                                            // up on the clipboard either way, and the
                                            // item is offered whether or not one
                                            // already exists.
                                            "label": ctx => qsTr("Copy link"),
                                            "group": "share",
                                            "trigger": ctx => ctx.mutations.copyLinkToClipboard(
                                                                  ctx.handle)
                                        },
                                        // ctx: requestLinkSettings(). Routed via the
                                        // view like removeLink below, since what it
                                        // opens is an Item no singleton can reach.
                                        // Offered whether or not a link exists: the
                                        // dialog exports on open, so there is always
                                        // something to show.
                                        "linkSettings": {
                                            "icon": ctx => Theme.glyph.menu.linkSettings,
                                            "label": ctx => qsTr("Link settings"),
                                            "group": "share",
                                            "trigger": ctx => ctx.requestLinkSettings()
                                        },
                                        // ctx: exported, requestRemoveLink(). Routed
                                        // via the view's confirmation like
                                        // moveToRubbish, not straight to the
                                        // controller like copyLink above: removing
                                        // the link revokes access for everyone
                                        // already holding it.
                                        "removeLink": {
                                            "icon": ctx => Theme.glyph.menu.removeLink,
                                            "label": ctx => qsTr("Remove link"),
                                            "group": "share",
                                            // Greyed rather than hidden, so the row
                                            // stays where the user learnt it was.
                                            // disableExport succeeds on an unshared
                                            // node anyway, so this is about saying
                                            // there is nothing to remove.
                                            "enabled": ctx => ctx.exported === true,
                                            "trigger": ctx => ctx.requestRemoveLink()
                                        },
                                        // ctx: entries, navController
                                        "cut": {
                                            "icon": ctx => Theme.glyph.menu.cut,
                                            "label": ctx => qsTr("Cut"),
                                            "trigger": ctx => clipboardController.cut(ctx.entries,
                                                                                      ctx.navController.currentHandle,
                                                                                      ctx.navController.atRoot)
                                        },
                                        // ctx: entries, navController
                                        "copy": {
                                            "icon": ctx => Theme.glyph.menu.copy,
                                            "label": ctx => qsTr("Copy"),
                                            "trigger": ctx => clipboardController.copy(ctx.entries,
                                                                                       ctx.navController.currentHandle,
                                                                                       ctx.navController.atRoot)
                                        },
                                        // ctx: canPaste, mutations
                                        "paste": {
                                            "icon": ctx => Theme.glyph.menu.paste,
                                            "label": ctx => qsTr("Paste"),
                                            // Greyed rather than hidden, same as togglePin above:
                                            // a row that comes and goes with the clipboard reads
                                            // worse than one that is simply unavailable.
                                            "enabled": ctx => ctx.canPaste,
                                            "trigger": ctx => ctx.mutations.paste()
                                        },
                                        // ctx: requestRename()
                                        "rename": {
                                            "icon": ctx => Theme.glyph.menu.rename,
                                            "label": ctx => qsTr("Rename"),
                                            "trigger": ctx => ctx.requestRename()
                                        },
                                        // ctx: requestMoveToRubbish()
                                        "moveToRubbish": {
                                            "icon": ctx => Theme.glyph.menu.moveToRubbish,
                                            "label": ctx => qsTr("Move to Rubbish bin"),
                                            "trigger": ctx => ctx.requestMoveToRubbish()
                                        },
                                        // ctx: entries, mutations. Straight to the
                                        // controller, unlike moveToRubbish's
                                        // request*(): there is no confirmation to
                                        // route through a dialog, since restoring
                                        // destroys nothing.
                                        "restore": {
                                            "icon": ctx => Theme.glyph.menu.restore,
                                            "label": ctx => qsTr("Restore"),
                                            "trigger": ctx => ctx.mutations.restoreHandles(
                                                                  ctx.entries.map(e => e.handle))
                                        },
                                        // ctx: requestDeletePermanently(). Through a
                                        // request*() rather than straight to the
                                        // controller like restore above, and for the
                                        // opposite reason: this one destroys, so it
                                        // is routed via the view's confirmation.
                                        "deletePermanently": {
                                            "icon": ctx => Theme.glyph.menu.deletePermanently,
                                            "label": ctx => qsTr("Delete permanently"),
                                            "trigger": ctx => ctx.requestDeletePermanently()
                                        },
                                        // ctx: requestEmptyRubbish()
                                        "emptyRubbish": {
                                            "icon": ctx => Theme.glyph.menu.emptyRubbish,
                                            "label": ctx => qsTr("Empty Rubbish bin"),
                                            "trigger": ctx => ctx.requestEmptyRubbish()
                                        },
                                        // ctx: handle, name, navController
                                        "goToFolder": {
                                            "icon": ctx => Theme.glyph.menu.goToFolder,
                                            "label": ctx => qsTr("Go to folder"),
                                            "trigger": ctx => ctx.navController.goToContainingFolder(
                                                                  ctx.handle, ctx.name)
                                        },
                                        // ctx: navController
                                        "selectAll": {
                                            "icon": ctx => Theme.glyph.menu.selectAll,
                                            "label": ctx => qsTr("Select all"),
                                            "trigger": ctx
                                                       => ctx.navController.fileListModel.selectAll(
                                                              )
                                        },
                                        // ctx: navController, or treeRow + handle
                                        // + isRoot. Two targets because the C++
                                        // table offers this at FolderBackground and
                                        // FolderRow alike: from a view's empty space
                                        // it re-reads that view's listing, from a
                                        // folder-tree row that row's subfolders.
                                        //
                                        // Hidden on a Quick access pin, the third
                                        // thing FolderRow reaches: a pin is not a
                                        // tree node, so there is nothing below it to
                                        // re-read.
                                        "refresh": {
                                            "icon": ctx => Theme.glyph.menu.refresh,
                                            "label": ctx => qsTr("Refresh"),
                                            "available": ctx => ctx.treeRow === true
                                                        || ctx.navController !== undefined,
                                            "trigger": ctx => ctx.treeRow === true
                                                       ? folderTreeModel.refreshFolder(ctx.handle,
                                                                                       ctx.isRoot)
                                                       : ctx.navController.refresh()
                                        },
                                        // ctx: isRoot, entries. Straight to the
                                        // app-wide controller rather than a
                                        // request*(): the dialog is one instance in
                                        // Main.qml, so no per-view Item has to be
                                        // reached.
                                        //
                                        // entries[0] is the primary target at both
                                        // sites this action is offered at: the one
                                        // selected row, or -- from a view's empty
                                        // space -- the folder the view is showing,
                                        // which is the only target that can be a
                                        // root and so the only reason isRoot is read.
                                        "properties": {
                                            "icon": ctx => Theme.glyph.menu.properties,
                                            "label": ctx => qsTr("Properties"),
                                            "trigger": ctx => propertiesController.show(
                                                          ctx.entries[0].handle,
                                                          ctx.isRoot === true,
                                                          ctx.entries[0].name,
                                                          ctx.entries[0].isFolder,
                                                          ctx.entries[0].sizeBytes,
                                                          ctx.entries[0].modificationTime)
                                        }
                                    })

    // Submenus, keyed by the `group` an entry names. Which members a group
    // holds, and in what order, is still the resolver's call: a group is only
    // how ActionMenu.qml presents the IDs it was handed.
    readonly property var groups: ({
                                       "openAs": {
                                           "icon": Theme.glyph.menu.openAs,
                                           "label": qsTr("Open as")
                                       },
                                       // "with", not "as": its members hand the node
                                       // to another program, where Open as stays in
                                       // this app and only overrides the extension.
                                       "openWith": {
                                           "icon": Theme.glyph.menu.openWith,
                                           "label": qsTr("Open with")
                                       },
                                       // Named after what its members act on -- the
                                       // counterpart under the linked local folder --
                                       // rather than after opening, which is what
                                       // keeps it apart from the MEGA node every
                                       // other action here names.
                                       "localPath": {
                                           "icon": Theme.glyph.menu.openLocalLocation,
                                           "label": qsTr("Local path")
                                       },
                                       "share": {
                                           "icon": Theme.glyph.menu.share,
                                           "label": qsTr("Share")
                                       }
                                   })

    // The prefix C++ agrees on for a user-registered program (src/core/
    // OpenWithEntry.h). The index after the colon is this file's only way to
    // reach an entry, since the C++ vocabulary has one placeholder for the
    // whole list.
    readonly property string customOpenWithPrefix: "openWithCustom:"

    // Synthesized rather than listed in `entries` above: how many there are is
    // a setting, so the rows cannot be written out one per ID. Shape matches an
    // `entries` member exactly, which is what lets every accessor below go
    // through lookup() and stay unaware of the difference.
    function customOpenWithEntry(index) {
        return {
            "icon": ctx => Theme.glyph.menu.openWithProgram,
            "label": ctx => openWithController.nameAt(index),
            "group": "openWith",
            // Greyed, not hidden, for a file the program does not handle: the
            // row is a fact about what is registered, so it should not come and
            // go with the selection (STUDY_OPEN_WITH.md 3-3).
            "enabled": ctx => openWithController.matchesAt(index, ctx.name),
            "trigger": ctx => openWithController.launch(index, ctx.handle)
        };
    }

    function lookup(actionId) {
        const entry = root.entries[actionId];
        if (entry !== undefined)
            return entry;
        if (typeof actionId !== "string" || !actionId.startsWith(root.customOpenWithPrefix))
            return undefined;
        const index = parseInt(actionId.slice(root.customOpenWithPrefix.length), 10);
        if (isNaN(index) || index < 0 || index >= openWithController.count)
            return undefined;
        return root.customOpenWithEntry(index);
    }

    // Replaces the resolver's single "openWithCustom" placeholder with one ID
    // per registered program, in registration order. Done here rather than in
    // C++ because the count is a setting the resolver cannot see, and appending
    // is enough to place them: rows() puts a group where its first member is,
    // so they land under the built-in Browser either way.
    function expand(actionIds) {
        const result = [];
        for (const id of actionIds) {
            if (id !== "openWithCustom") {
                result.push(id);
                continue;
            }
            for (let i = 0; i < openWithController.count; ++i)
                result.push(root.customOpenWithPrefix + i);
        }
        return result;
    }

    // Folds an ordered ID list into the rows a menu shows: {id} for an action
    // of its own, {group, ids} for a submenu. A group takes the position of its
    // first member and keeps its members in the order they arrived.
    function rows(actionIds) {
        const result = [];
        const groupRows = {};
        for (const id of actionIds) {
            const entry = root.lookup(id);
            const group = entry === undefined ? undefined : entry.group;
            if (group === undefined) {
                result.push({
                                "id": id
                            });
                continue;
            }
            if (groupRows[group] === undefined) {
                groupRows[group] = {
                    "group": group,
                    "ids": []
                };
                result.push(groupRows[group]);
            }
            groupRows[group].ids.push(id);
        }
        return result;
    }

    function groupLabel(group) {
        return root.groups[group].label;
    }

    function groupIcon(group) {
        return root.groups[group].icon;
    }

    // undefined (not "") for an unrecognized ID, so ActionMenu.qml can tell
    // "no such action" apart from "an action deliberately labelled empty" and
    // disable the item instead of showing a blank enabled row.
    function label(actionId, ctx) {
        const entry = root.lookup(actionId);
        return entry === undefined ? undefined : entry.label(ctx);
    }

    // "" (not undefined) for an unknown ID, because the caller feeds this
    // straight to IconMenuItem.glyph: a row with no icon still keeps the
    // gutter, so an unrecognized action lines up with the rest instead of
    // starting at the left edge. The code points themselves live in
    // Theme.glyph.menu -- this file only decides which action gets which.
    function icon(actionId, ctx) {
        const entry = root.lookup(actionId);
        return entry === undefined ? "" : entry.icon(ctx);
    }

    // Entries without an `enabled` are always enabled -- greying is the
    // exception, so it isn't worth a no-op function in every entry.
    //
    // `=== true` rather than the raw result: ActionMenu.qml's delegates are
    // built when the menu object is, while ctx is still the empty default, so
    // a predicate reading a ctx field it hasn't been given yet (paste's
    // ctx.canPaste) hands back undefined -- which QML then refuses to assign
    // to the bool `enabled`.
    function isEnabled(actionId, ctx) {
        const entry = root.lookup(actionId);
        if (entry === undefined)
            return false;
        return entry.enabled === undefined ? true : entry.enabled(ctx) === true;
    }

    // Whether the action should appear at all, as opposed to appear greyed --
    // `enabled` above answers the latter. Only for conditions the C++ resolver
    // cannot see, i.e. app-wide state that no MenuContext axis carries; anything
    // derivable from the selection belongs in MenuActionResolver's table instead.
    //
    // Entries without an `available` are always shown, and an unknown ID stays in
    // the list so ActionMenu.qml can render it as the disabled "None" row rather
    // than silently swallowing a catalog gap.
    function isAvailable(actionId, ctx) {
        const entry = root.lookup(actionId);
        if (entry === undefined)
            return true;
        return entry.available === undefined ? true : entry.available(ctx) === true;
    }

    function trigger(actionId, ctx) {
        const entry = root.lookup(actionId);
        if (entry !== undefined)
            entry.trigger(ctx);
    }
}
