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
//   canPaste              FolderBackground only: whether a paste would do
//                         anything, sampled when the menu opens
//
// Each entry documents which of those it actually reads.
QtObject {
    id: root

    readonly property var entries: ({
                                        // ctx: requestNewFolder()
                                        "newFolder": {
                                            "label": ctx => qsTr("New folder"),
                                            "trigger": ctx => ctx.requestNewFolder()
                                        },
                                        // ctx: entries
                                        "download": {
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
                                        // ctx: handle, isRoot
                                        "openInNewTab": {
                                            "label": ctx => qsTr("Open in new tab"),
                                            // Background tab, current tab keeps focus -- same
                                            // convention as the views' and the tree's middle-click.
                                            "trigger": ctx => tabsController.addTabAt(ctx.handle,
                                                                                      ctx.isRoot)
                                        },
                                        // ctx: handle, isRoot, name, pinned
                                        "togglePin": {
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
                                        // ctx: entries, navController
                                        "cut": {
                                            "label": ctx => qsTr("Cut"),
                                            "trigger": ctx => clipboardController.cut(ctx.entries,
                                                                                      ctx.navController.currentHandle,
                                                                                      ctx.navController.atRoot)
                                        },
                                        // ctx: entries, navController
                                        "copy": {
                                            "label": ctx => qsTr("Copy"),
                                            "trigger": ctx => clipboardController.copy(ctx.entries,
                                                                                       ctx.navController.currentHandle,
                                                                                       ctx.navController.atRoot)
                                        },
                                        // ctx: canPaste, navController
                                        "paste": {
                                            "label": ctx => qsTr("Paste"),
                                            // Greyed rather than hidden, same as togglePin above:
                                            // a row that comes and goes with the clipboard reads
                                            // worse than one that is simply unavailable.
                                            "enabled": ctx => ctx.canPaste,
                                            "trigger": ctx => ctx.navController.paste()
                                        },
                                        // ctx: requestRename()
                                        "rename": {
                                            "label": ctx => qsTr("Rename"),
                                            "trigger": ctx => ctx.requestRename()
                                        },
                                        // ctx: requestMoveToRubbish()
                                        "moveToRubbish": {
                                            "label": ctx => qsTr("Move to Rubbish bin"),
                                            "trigger": ctx => ctx.requestMoveToRubbish()
                                        },
                                        // ctx: navController
                                        "selectAll": {
                                            "label": ctx => qsTr("Select all"),
                                            "trigger": ctx
                                                       => ctx.navController.fileListModel.selectAll(
                                                              )
                                        },
                                        // ctx: navController
                                        "refresh": {
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

    // Entries without an `enabled` are always enabled -- greying is the
    // exception, so it isn't worth a no-op function in every entry.
    function isEnabled(actionId, ctx) {
        const entry = root.entries[actionId];
        if (entry === undefined)
            return false;
        return entry.enabled === undefined ? true : entry.enabled(ctx);
    }

    function trigger(actionId, ctx) {
        const entry = root.entries[actionId];
        if (entry !== undefined)
            entry.trigger(ctx);
    }
}
