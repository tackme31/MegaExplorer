import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml/Breadcrumb.qml/FileTableView.qml.
import QtQuick.Controls.FluentWinUI3

// Every right-click menu in the app. Takes an ordered list of stable action
// IDs (from FileListModel::availableActions or the MenuActions singleton) and
// a context object, and builds the items from ActionCatalog.qml -- so a new
// action is one C++ table row plus one catalog entry, never a new Menu.
//
// One instance per view, never one per delegate (Phase 13b's lesson: a
// delegate-scoped Menu meant one live Popup per cell, e.g. 3000 for a
// 1000-row x 3-column TableView). Menu is a Popup, not an Item, so it isn't
// laid out by its parent and isn't clipped by a Flickable viewport; a
// parentless popup() opens at the mouse cursor wherever the object lives in
// the tree.
Menu {
    id: root

    required property var actionIds

    // Assigned wholesale immediately before opening, never bound to live
    // state: a menu must not be able to change its wording or its target out
    // from under the user while it is open. Replacing the object is what
    // re-evaluates the bindings below.
    property var context: ({})

    // Instantiator (Qt's "Dynamically Generating Menu Items" pattern) rather
    // than a Repeater: Menu's contentItem isn't a plain Item container a
    // Repeater can target. This also avoids the old visible+height:0 hack's
    // leftover ListView spacing (FluentWinUI3's Menu content is a spacing:4
    // ListView) -- an item that doesn't exist reserves no spacing, unlike one
    // that's merely invisible.
    Instantiator {
        // Zero applicable actions still needs one disabled "None" row rather
        // than an empty, unopenable menu -- [""] guarantees that, and also
        // covers an action ID the catalog hasn't been updated for yet.
        model: root.actionIds.length > 0 ? root.actionIds : [""]

        delegate: IconMenuItem {
            required property string modelData

            // var, not string: a string-typed property coerces an undefined
            // lookup (unrecognized action ID) to "" instead of preserving
            // undefined, which would silently defeat the enabled check below.
            readonly property var label: ActionCatalog.label(modelData, root.context)

            text: label !== undefined ? label : qsTr("None")
            glyph: ActionCatalog.icon(modelData, root.context)
            enabled: label !== undefined && ActionCatalog.isEnabled(modelData, root.context)

            onTriggered: ActionCatalog.trigger(modelData, root.context)
        }

        onObjectAdded: (index, object) => root.insertItem(index, object)
        onObjectRemoved: (index, object) => root.removeItem(object)
    }
}
