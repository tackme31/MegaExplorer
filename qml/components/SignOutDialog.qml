import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls

// One instance for the whole app, in Main.qml. Unlike the other three dialogs
// there it carries no state and wires nothing: AddressToolBar relays a
// signOutRequested up to Main.qml, which open()s this.
Dialog {
    id: root

    required property var auth
    required property var downloads
    required property var uploads

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    standardButtons: Dialog.Yes | Dialog.Cancel
    Component.onCompleted: StandardButtonLabels.pin(footer)

    // Neither DownloadService nor UploadService has a cancel API yet, so an
    // in-flight transfer is simply aborted by logout(). Warn about it up
    // front rather than silently dropping it.
    title: (root.downloads.downloadActive || root.uploads.uploadActive) ? qsTr(
                                                                              "Sign out? (transfer in progress)") :
                                                                          qsTr("Sign out?")

    onAccepted: root.auth.logout()
}
