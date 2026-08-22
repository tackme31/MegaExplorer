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

    // Needed by toggleFavourite and copyLink; the rest of this site's actions
    // reach their targets through singletons or the request*() callbacks below.
    required property var mutController

    // Delegated to the owning view rather than handled in ActionCatalog.qml:
    // each view owns its own inline rename field and ConfirmRubbishDialog
    // instance, which a singleton can't reach.
    signal renameRequested
    signal moveToRubbishRequested
    signal deletePermanentlyRequested
    signal removeLinkRequested

    // Sampled by sampleActions() below, never bound to availableActions: that
    // property is notified by selectionChanged, and the Instantiator behind
    // actionIds rebuilds every MenuItem whenever the list's contents differ --
    // ~77ms per menu, twice per tab, on the GUI thread, every time the applicable
    // set changed (typically empty -> non-empty after a click on empty space).
    // Same "assign wholesale immediately before opening" rule as context below.
    actionIds: []

    // Called by the owning view right before popup(), rather than from
    // onAboutToShow: adding and removing items is a bigger change than the
    // wording swap context does, and doing it outside the popup's own open
    // sequence keeps it clear of menu sizing and positioning.
    //
    // context is assigned here and *before* actionIds, rather than from
    // onAboutToShow: assigning actionIds is what makes the Instantiator build
    // the MenuItems, so doing it second means they are built with their real
    // labels instead of the previous menu's, and never briefly read a ctx that
    // has nothing in it. (ActionMenu measures its own width on aboutToShow, so
    // this ordering is correctness, not the elision fix it looks like.)
    function sampleActions() {
        root.context = root.buildContext();
        // Filtered here rather than bound into ActionMenu.qml's Instantiator: the
        // model there would then re-evaluate on the context assignment above too,
        // rebuilding every MenuItem twice per open.
        root.actionIds = root.navController.fileListModel.availableActions.filter(
                    actionId => ActionCatalog.isAvailable(actionId, root.context));
    }

    function buildContext() {
        const entries = root.navController.fileListModel.selectedEntries();
        const primary = entries.length > 0 ? entries[0] : {
                                                 "handle": 0,
                                                 "name": ""
                                             };
        return {
            "handle": primary.handle,
            // A selected row is a child of the folder being shown, so it is
            // never the Cloud Drive root itself.
            "isRoot": false,
            "name": primary.name,
            // Sampled here rather than bound: quickAccessModel emits no
            // per-handle change signal, so a binding would never re-evaluate
            // anyway -- and the menu is closed whenever the answer could change.
            "pinned": entries.length === 1 && quickAccessModel.isPinned(primary.handle),
            // Sampled, not bound, for the same reason as pinned above: the model
            // can only change the answer while the menu is closed.
            "favourited": entries.length === 1 && entries[0].isFavourite === true,
            // Sampled like favourited above. Only "Remove link" reads it: "Copy
            // link" exports on demand, so it is offered either way.
            "exported": entries.length === 1 && entries[0].isExported === true,
            // Sampled like pinned/favourited above, and it can only change while
            // the menu is closed -- the settings dialog is modal.
            "localFolderLinked": localFolderController.linked,
            "entries": entries,
            "navController": root.navController,
            "mutations": root.mutController,
            "requestRename": () => root.renameRequested(),
            "requestMoveToRubbish": () => root.moveToRubbishRequested(),
            "requestDeletePermanently": () => root.deletePermanentlyRequested(),
            "requestRemoveLink": () => root.removeLinkRequested()
        };
    }
}
