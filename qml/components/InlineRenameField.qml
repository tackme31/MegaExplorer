import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3

// Explorer-style in-place rename editor, shared by both file views
// (FileTableView.qml's name cell and FileGridView.qml's tile). Deliberately
// not Qt 6.5's TableView.editDelegate/edit(): that machinery moves the
// selectionModel's current index, which these views don't have (Phase 13a's
// selection lives in FileListModel, keyed by handle), its default editTriggers
// collide with double-click-to-open, and GridView has no equivalent at all --
// so one hand-rolled field used by both views is both less code and
// consistent. See docs/PROGRESS.md's Phase 12 log.
//
// Instantiated by a Loader that goes active only for the row being renamed, so
// a large listing pays nothing for this.
TextField {
    id: field

    required property string originalName
    // Folders have no extension to preserve, so the whole name is selected --
    // matching Explorer.
    required property bool isFolder

    signal committed(string newName)
    signal cancelled

    // Every exit path is final: confirming hands focus back to the view, which
    // fires onActiveFocusChanged below and would otherwise commit the same
    // edit a second time.
    property bool settled: false

    function commit() {
        if (field.settled)
            return;
        field.settled = true;
        field.committed(field.text);
    }

    function cancel() {
        if (field.settled)
            return;
        field.settled = true;
        field.cancelled();
    }

    Component.onCompleted: {
        field.text = field.originalName;
        field.forceActiveFocus();
        const dot = field.originalName.lastIndexOf(".");
        // dot > 0, not >= 0: a leading dot is the whole name of a dotfile, not
        // an extension separator.
        if (!field.isFolder && dot > 0)
            field.select(0, dot);
        else
            field.selectAll();
    }

    // TextInput emits accepted for both Return and Enter, so this covers both
    // without a Keys handler.
    onAccepted: field.commit()

    Keys.onEscapePressed: event => {
        field.cancel();
        event.accepted = true;
    }

    // Clicking anywhere outside confirms, same as Explorer. Clicks *inside*
    // this field must not reach here at all -- see the guard in each view's
    // background TapHandler for why that needs explicit handling.
    onActiveFocusChanged: if (!field.activeFocus)
                              field.commit()
}
