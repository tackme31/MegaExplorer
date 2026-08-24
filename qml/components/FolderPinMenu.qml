import QtQuick

// Right-click menu for a folder row addressed by (handle, isRoot) -- shared by
// FolderTreePanel.qml's tree rows and QuickAccessSection.qml's pin rows.
// Distinct from FileContextMenu.qml, which is driven by the file list's
// *selection* and knows nothing about which row was clicked; here there is no
// selection model at all, so the target is passed in explicitly.
//
// The items and their order come from the same C++ table every other menu
// uses (MenuSite::FolderRow in MenuActionResolver's defaultMenuActions()), so
// there is nothing left to keep in step by hand -- an action shared with the
// file views can no longer come out in a different order here.
ActionMenu {
    id: root

    // Always Cloud Drive: a tree row and a pin both name a real folder, whatever the
    // file view beside them happens to be showing.
    readonly property var siteActionIds: MenuActions.forSite(MenuActions.FolderRow,
                                                             ViewKind.CloudDrive)
    actionIds: root.siteActionIds

    // Set by the folder tree, left false by the pins: only a tree row has a subtree
    // that "Refresh" could re-read. Carried in the context below rather than read
    // from the catalog, which cannot see which menu instance opened.
    property bool treeRow: false

    // Fills in the context immediately before opening rather than binding to
    // anything: this describes one particular click.
    function popupFor(handle, isRoot, name) {
        root.context = {
            "handle": handle,
            "isRoot": isRoot,
            "name": name,
            "treeRow": root.treeRow,
            // Sampled at open time, not bound: quickAccessModel emits no
            // per-handle change signal, so a binding would never re-evaluate
            // anyway -- and the menu is closed whenever the answer could change.
            "pinned": !isRoot && quickAccessModel.isPinned(handle),
            "entries": [
                {
                    "handle": handle,
                    "name": name,
                    "sizeBytes": 0,
                    "isFolder": true
                }
            ]
        };
        // Filtered here, after the context and before popup(): ActionMenu.qml
        // applies no availability rule of its own, so an entry hidden by one --
        // Refresh on a pin row -- would otherwise still be listed. Same
        // context-then-actionIds ordering as FileContextMenu.qml, for the reason
        // given there.
        root.actionIds = root.siteActionIds.filter(
                    actionId => ActionCatalog.isAvailable(actionId, root.context));
        root.popup();
    }
}
