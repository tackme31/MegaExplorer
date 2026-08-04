import QtQuick
import QtQuick.Controls.FluentWinUI3

// Selection-driven, one instance per view (FileTableView.qml / Main.qml),
// not per delegate as before -- a delegate-scoped Menu meant one live Menu
// per cell (e.g. 3000 for a 1000-row x 3-column TableView). Right-click
// handlers already call selectRow() before popup() when the clicked row
// isn't already selected, so this menu only ever needs the current
// selection, never which item was clicked.
Menu {
    id: root

    required property var navController

    // Display text for each stable action ID from
    // FileListModel::availableActions (see FileActionResolver::fileActionId).
    // C++ passes structured IDs, QML supplies user-facing text -- same split
    // as NotificationController/ToastStack.qml.
    readonly property var actionLabels: ({
                                             "download": qsTr("Download"),
                                             "openInNewTab": qsTr("Open in new tab"),
                                             "rename": qsTr("Rename"),
                                             "moveToRubbish": qsTr("Move to Rubbish bin")
                                         })

    // Both delegated to the owning view rather than handled here: this file
    // knows nothing about inline editing or dialogs, and each view owns its own
    // rename field / ConfirmRubbishDialog instance.
    signal renameRequested
    signal moveToRubbishRequested

    // "togglePin" is one action with two labels -- FileActionResolver only sees
    // selection counts, so it can't know whether the folder is already pinned
    // (see FileAction.h). Sampled once per open rather than bound: there's no
    // per-handle change signal on quickAccessModel to drive a binding, and the
    // answer can't change while the menu is open.
    property bool selectionPinned: false

    onAboutToShow: {
        const entries = root.navController.fileListModel.selectedEntries();
        root.selectionPinned = entries.length === 1 && quickAccessModel.isPinned(entries[0].handle);
    }

    // Optional-chained: closing a tab clears the Repeater delegate's model role
    // before the pane (and this menu with it) is actually deleted, so
    // navController is null for the one binding re-evaluation in between.
    readonly property var availableActions: root.navController?.fileListModel?.availableActions
                                            ?? []

    function labelFor(actionId) {
        if (actionId === "togglePin")
            return root.selectionPinned ? qsTr("Unpin from Quick access") : qsTr(
                                              "Pin to Quick access");
        return root.actionLabels[actionId];
    }

    // Instantiator (Qt's "Dynamically Generating Menu Items" pattern) rather
    // than a Repeater: Menu's contentItem isn't a plain Item container a
    // Repeater can target. This also drops the old visible+height:0 hack's
    // leftover ListView spacing (FluentWinUI3's Menu content is a spacing:4
    // ListView) -- an item that doesn't exist reserves no spacing, unlike
    // one that's merely invisible.
    Instantiator {
        // Zero applicable actions (or a mixed/folder selection) still needs
        // one disabled "None" row rather than an empty, unopenable menu --
        // [""] guarantees that, and also covers an unrecognized future
        // action ID that this map hasn't been updated for yet.
        model: root.availableActions.length > 0 ? root.availableActions : [""]

        delegate: MenuItem {
            required property string modelData

            // var, not string: a string-typed property coerces an undefined
            // lookup (unrecognized action ID) to "" instead of preserving
            // undefined, which would silently defeat the enabled check below.
            readonly property var label: root.labelFor(modelData)

            text: label !== undefined ? label : qsTr("None")
            enabled: label !== undefined

            onTriggered: {
                if (modelData === "download") {
                    const entries = root.navController.fileListModel.selectedEntries();
                    for (let i = 0; i < entries.length; ++i) {
                        downloadController.downloadFile(entries[i].handle, entries[i].name,
                                                        entries[i].sizeBytes);
                    }
                } else if (modelData === "openInNewTab") {
                    const entries = root.navController.fileListModel.selectedEntries();
                    if (entries.length > 0)
                        tabsController.addTabAt(entries[0].handle, false);
                } else if (modelData === "togglePin") {
                    const entries = root.navController.fileListModel.selectedEntries();
                    if (entries.length > 0) {
                        if (root.selectionPinned)
                            quickAccessModel.unpin(entries[0].handle);
                        else
                            quickAccessModel.pin(entries[0].handle, entries[0].name);
                    }
                } else if (modelData === "rename") {
                    root.renameRequested();
                } else if (modelData === "moveToRubbish") {
                    root.moveToRubbishRequested();
                }
            }
        }

        onObjectAdded: (index, object) => root.insertItem(index, object)
        onObjectRemoved: (index, object) => root.removeItem(object)
    }
}
