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
//   entries               every target, as {handle, name, sizeBytes, isFolder}
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
                                            "label": ctx => qsTr("Open local file"),
                                            // Hidden without a linked folder, for
                                            // the same reason as openLocalLocation
                                            // below.
                                            "available": ctx => ctx.localFolderLinked === true,
                                            "trigger": ctx => localFolderController.openFile(
                                                          ctx.handle)
                                        },
                                        // ctx: handle, localFolderLinked
                                        "openLocalLocation": {
                                            "icon": ctx => Theme.glyph.menu.openLocalLocation,
                                            "label": ctx => qsTr("Open local location"),
                                            // Hidden rather than greyed, unlike
                                            // paste below: greying advertises a
                                            // feature to everyone who never linked
                                            // a folder, and the C++ resolver has no
                                            // axis for an app-wide setting.
                                            "available": ctx => ctx.localFolderLinked === true,
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
                                            "trigger": ctx => ctx.mutations.copyLinkToClipboard(
                                                                  ctx.handle)
                                        },
                                        // ctx: requestRemoveLink(). Routed via the
                                        // view's confirmation like moveToRubbish,
                                        // not straight to the controller like
                                        // copyLink above: removing the link revokes
                                        // access for everyone already holding it.
                                        "removeLink": {
                                            "icon": ctx => Theme.glyph.menu.removeLink,
                                            "label": ctx => qsTr("Remove link"),
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
                                        // ctx: navController
                                        "refresh": {
                                            "icon": ctx => Theme.glyph.menu.refresh,
                                            "label": ctx => qsTr("Refresh"),
                                            "trigger": ctx => ctx.navController.refresh()
                                        }
                                    })

    // undefined (not "") for an unrecognized ID, so ActionMenu.qml can tell
    // "no such action" apart from "an action deliberately labelled empty" and
    // disable the item instead of showing a blank enabled row.
    function label(actionId, ctx) {
        const entry = root.entries[actionId];
        return entry === undefined ? undefined : entry.label(ctx);
    }

    // "" (not undefined) for an unknown ID, because the caller feeds this
    // straight to IconMenuItem.glyph: a row with no icon still keeps the
    // gutter, so an unrecognized action lines up with the rest instead of
    // starting at the left edge. The code points themselves live in
    // Theme.glyph.menu -- this file only decides which action gets which.
    function icon(actionId, ctx) {
        const entry = root.entries[actionId];
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
        const entry = root.entries[actionId];
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
        const entry = root.entries[actionId];
        if (entry === undefined)
            return true;
        return entry.available === undefined ? true : entry.available(ctx) === true;
    }

    function trigger(actionId, ctx) {
        const entry = root.entries[actionId];
        if (entry !== undefined)
            entry.trigger(ctx);
    }
}
