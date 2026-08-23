import QtQuick

// Right-click menu for a file view's empty space. Its target is the folder the
// view is currently showing -- not the selection, which the views clear before
// opening this -- so it is the one menu whose target exists even when nothing
// is selected, the case MenuActionResolver's selection-shaped axes can't
// express on their own (hence MenuSite::FolderBackground).
ActionMenu {
    id: root

    required property var navController
    required property var mutController

    // Delegated to the owning view, which relays it to the tab's single
    // NewFolderDialog -- one dialog per tab, not per view, since both views
    // share the tab's FileMutationController and would otherwise both react to
    // its result signals.
    signal newFolderRequested

    // Delegated for the same reason: the confirmation is a dialog the owning view
    // holds, not something a singleton catalog entry can reach.
    signal emptyRubbishRequested

    // The ternary is not redundant: a tab's controllers are destroyed before the
    // views holding this menu are, so the binding re-evaluates once against a null
    // navController on the way out.
    actionIds: root.navController
        ? MenuActions.forSite(MenuActions.FolderBackground, root.navController.viewKind)
        : []

    onAboutToShow: {
        // The folder on screen, shaped like a listing row so the catalog entries
        // shared with the selection menu (properties) need no site-specific branch.
        // Its size and time are left at 0 deliberately: a breadcrumb carries neither,
        // and the dialog shows a folder's recursive total from its own lookup and
        // hides the modified row for folders entirely.
        const folder = {
            "handle": root.navController.currentHandle,
            "name": ViewLabels.label(root.navController.viewKind, root.navController.atRoot,
                                     root.navController.currentFolderName),
            "isFolder": true,
            "sizeBytes": 0,
            "modificationTime": 0
        };
        root.context = {
            "handle": folder.handle,
            "isRoot": root.navController.atRoot,
            "name": folder.name,
            "pinned": false,
            "entries": [folder],
            "navController": root.navController,
            "mutations": root.mutController,
            // Sampled, not bound: a menu must not grey or un-grey a row while
            // it is open (see ActionMenu.qml).
            "canPaste": root.mutController.canPaste(),
            "requestNewFolder": () => root.newFolderRequested(),
            "requestEmptyRubbish": () => root.emptyRubbishRequested()
        };
    }
}
