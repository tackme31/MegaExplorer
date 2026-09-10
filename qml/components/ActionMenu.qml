import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml/Breadcrumb.qml/FileTableView.qml.
import QtQuick.Controls.FluentWinUI3

// Every right-click menu in the app. Takes an ordered list of stable action
// IDs (from FileListModel::availableActions or the MenuActions singleton) and
// a context object, and builds the items from ActionCatalog.qml -- so a new
// action is one C++ table row plus one catalog entry, never a new Menu.
// Actions the catalog puts in a group (the link actions under "Share") become
// one submenu row, at the position of the group's first member.
//
// One instance per view, never one per delegate (Phase 13b's lesson: a
// delegate-scoped Menu meant one live Popup per cell, e.g. 3000 for a
// 1000-row x 3-column TableView). Menu is a Popup, not an Item, so it isn't
// laid out by its parent and isn't clipped by a Flickable viewport; a
// parentless popup() opens at the mouse cursor wherever the object lives in
// the tree.
MeasuredMenu {
    id: root

    required property var actionIds

    // Assigned wholesale immediately before opening, never bound to live
    // state: a menu must not be able to change its wording or its target out
    // from under the user while it is open. Replacing the object is what
    // re-evaluates the bindings below.
    property var context: ({})

    // Only ever used by addMenu() below, for a group's row -- the action rows
    // are created from actionRow directly. IconMenuItem so the group's row
    // keeps the glyph gutter the rows around it have.
    delegate: IconMenuItem {
        id: groupRow
        glyph: (groupRow.subMenu as MeasuredMenu)?.glyph ?? ""
    }

    Component {
        id: actionRow

        IconMenuItem {
            required property string actionId

            // var, not string: a string-typed property coerces an undefined
            // lookup (unrecognized action ID) to "" instead of preserving
            // undefined, which would silently defeat the enabled check below.
            readonly property var label: ActionCatalog.label(actionId, root.context)

            text: label !== undefined ? label : qsTr("None")
            glyph: ActionCatalog.icon(actionId, root.context)
            enabled: label !== undefined && ActionCatalog.isEnabled(actionId, root.context)

            onTriggered: ActionCatalog.trigger(actionId, root.context)
        }
    }

    Component {
        id: groupMenu

        MeasuredMenu {}
    }

    // By hand rather than through an Instantiator: a group has to go in through
    // addMenu(), and an Instantiator has one delegate for every row. Each rebuild
    // replaces every row, which is what the Instantiator did on a new model too.
    function rebuild() {
        while (root.count > 0) {
            const submenu = root.menuAt(0);
            if (submenu)
                root.removeMenu(submenu);
            else
                root.removeItem(root.itemAt(0));
        }

        // Zero applicable actions still needs one disabled "None" row rather
        // than an empty, unopenable menu -- [""] guarantees that, and also
        // covers an action ID the catalog hasn't been updated for yet.
        const rows = ActionCatalog.rows(root.actionIds.length > 0 ? root.actionIds : [""]);
        for (const row of rows) {
            if (row.group === undefined) {
                root.addItem(actionRow.createObject(root.contentItem, {
                                                        "actionId": row.id
                                                    }));
                continue;
            }
            const submenu = groupMenu.createObject(root, {
                                                       "title": ActionCatalog.groupLabel(row.group),
                                                       "glyph": ActionCatalog.groupIcon(row.group)
                                                   });
            for (const id of row.ids)
                submenu.addItem(actionRow.createObject(submenu.contentItem, {
                                                           "actionId": id
                                                       }));
            root.addMenu(submenu);
        }
    }

    Connections {
        target: root
        function onActionIdsChanged() {
            root.rebuild();
        }
    }

    Component.onCompleted: root.rebuild()
}
