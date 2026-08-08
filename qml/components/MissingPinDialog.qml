import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls

// One instance for the whole app, in Main.qml. Raised when a quick-access pin
// turns out to point at a folder that no longer exists -- only reachable for a
// folder deleted *during* this session (e.g. moved to the Rubbish bin here, or
// deleted on another device), since the login-time sweep in
// QuickAccessModel::reload silently drops the ones already gone. Declining
// leaves the pin in place, so clicking it again asks again.
//
// Only a *definitive* answer gets here. A check that couldn't be answered at
// all raises the quickAccessUnavailable toast instead, because offering to
// delete a pin on the strength of a failed lookup is exactly the bug this
// split was made to avoid.
Dialog {
    id: root

    required property var quickAccess

    // Carried on the dialog rather than remembered in C++, so it stays alive
    // exactly as long as the question does. pinHandle is `property var`
    // because a quint64 doesn't survive QML's int/real property types.
    property var pinHandle: 0
    property string pinName: ""

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    standardButtons: Dialog.Yes | Dialog.Cancel
    title: qsTr("Folder no longer exists")

    Label {
        text: qsTr("\"%1\" could not be found. Remove it from Quick access?").arg(root.pinName)
    }

    onAccepted: root.quickAccess.unpin(root.pinHandle)

    // QuickAccessModel verifies a pin's target before anything happens, then
    // reports back -- it deliberately knows nothing about tabs or dialogs. Its
    // other signal, activated(), is Main.qml's to handle.
    Connections {
        target: root.quickAccess
        function onMissing(handle, name) {
            root.pinHandle = handle;
            root.pinName = name;
            root.open();
        }
    }
}
