import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml/Breadcrumb.qml/FileTableView.qml.
import QtQuick.Controls.FluentWinUI3

// Right-click menu for a folder row that is addressed by (handle, isRoot) --
// shared by FolderTreePanel.qml's tree rows and QuickAccessSection.qml's pin
// rows, which want the same two actions. Distinct from FileContextMenu.qml,
// which is driven by the file list's *selection* and knows nothing about which
// row was clicked; here there is no selection model at all, so the target is
// passed in explicitly.
//
// The item order below is hardcoded but must stay in step with
// FileActionResolver's defaultFileActions() table, which is what orders
// FileContextMenu.qml -- the same two actions appearing in a different order
// depending on where you right-clicked reads as a bug.
//
// One instance per view, never one per delegate (Phase 13b's lesson: a
// delegate-scoped Menu means one live Popup per row). Menu is a Popup, not an
// Item, so it isn't laid out by its parent and a parentless popup() opens at
// the mouse cursor wherever the object lives in the tree.
Menu {
    id: root

    // Filled in by popupFor() immediately before opening, rather than bound to
    // anything: these describe one particular click.
    property var targetHandle: 0
    property bool targetIsRoot: false
    property string targetName: ""
    property bool targetPinned: false

    function popupFor(handle, isRoot, name) {
        root.targetHandle = handle;
        root.targetIsRoot = isRoot;
        root.targetName = name;
        // Sampled at open time, not bound: quickAccessModel emits no
        // per-handle change signal, so a binding would never re-evaluate
        // anyway -- and the menu is closed whenever the answer could change.
        root.targetPinned = !isRoot && quickAccessModel.isPinned(handle);
        root.popup();
    }

    MenuItem {
        text: qsTr("Open in new tab")
        // Background tab, current tab keeps focus -- same convention as the
        // file views' and the tree's middle-click.
        onTriggered: tabsController.addTabAt(root.targetHandle, root.targetIsRoot)
    }

    MenuItem {
        // The Cloud Drive root is permanently the tree's own top row, so
        // pinning it could only duplicate it -- Explorer doesn't allow it
        // either.
        enabled: !root.targetIsRoot
        text: root.targetPinned ? qsTr("Unpin from Quick access") : qsTr("Pin to Quick access")
        onTriggered: {
            if (root.targetPinned)
                quickAccessModel.unpin(root.targetHandle);
            else
                quickAccessModel.pin(root.targetHandle, root.targetName);
        }
    }
}
