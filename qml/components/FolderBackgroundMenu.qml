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
        root.context = {
            "handle": root.navController.currentHandle,
            "isRoot": root.navController.atRoot,
            "name": "",
            "pinned": false,
            "entries": [],
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
