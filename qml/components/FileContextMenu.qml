import QtQuick

// The file views' selection-driven menu: one instance per view, popped by the
// delegates' right-click handlers, which already call selectRow() before
// popup() when the clicked row isn't part of the selection -- so this menu
// only ever needs the current selection, never which item was clicked.
//
// Everything about *what the items are* lives in MenuActionResolver (C++,
// which actions) and ActionCatalog.qml (wording and execution); this file is
// just the FileSelection site's context.
ActionMenu {
    id: root

    required property var navController

    // Delegated to the owning view rather than handled in ActionCatalog.qml:
    // each view owns its own inline rename field and ConfirmRubbishDialog
    // instance, which a singleton can't reach.
    signal renameRequested
    signal moveToRubbishRequested

    // Optional-chained: closing a tab clears the Repeater delegate's model
    // role before the pane (and this menu with it) is actually deleted, so
    // navController is null for the one binding re-evaluation in between.
    actionIds: root.navController?.fileListModel?.availableActions ?? []

    onAboutToShow: {
        const entries = root.navController.fileListModel.selectedEntries();
        const primary = entries.length > 0 ? entries[0] : {
                                                 "handle": 0,
                                                 "name": ""
                                             };
        root.context = {
            "handle": primary.handle,
            // A selected row is a child of the folder being shown, so it is
            // never the Cloud Drive root itself.
            "isRoot": false,
            "name": primary.name,
            // Sampled here rather than bound: quickAccessModel emits no
            // per-handle change signal, so a binding would never re-evaluate
            // anyway -- and the menu is closed whenever the answer could change.
            "pinned": entries.length === 1 && quickAccessModel.isPinned(primary.handle),
            "entries": entries,
            "navController": root.navController,
            "requestRename": () => root.renameRequested(),
            "requestMoveToRubbish": () => root.moveToRubbishRequested()
        };
    }
}
