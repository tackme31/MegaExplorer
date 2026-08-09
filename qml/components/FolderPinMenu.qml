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
    actionIds: MenuActions.forSite(MenuActions.FolderRow, ViewKind.CloudDrive)

    // Fills in the context immediately before opening rather than binding to
    // anything: this describes one particular click.
    function popupFor(handle, isRoot, name) {
        root.context = {
            "handle": handle,
            "isRoot": isRoot,
            "name": name,
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
        root.popup();
    }
}
