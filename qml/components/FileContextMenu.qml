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
    function sampleActions() {
        root.actionIds = root.navController.fileListModel.availableActions;
    }

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
