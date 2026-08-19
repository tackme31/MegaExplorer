import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml/AboutDialog.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

// App-wide preferences, opened from the More menu. One instance for the whole
// app, in Main.qml -- which also owns the persisted value and is the only place
// that applies it, so this file decides nothing beyond "which item is showing".
//
// A language selector belongs here too, but nothing in
// the app is translated yet (no .ts files, no QTranslator), so the row would
// have exactly one choice; it lands with the i18n item on docs/ROADMAP.md.
Dialog {
    id: root

    // The stored preference, as a Qt::ColorScheme value: Qt.Unknown (0) means
    // "follow the OS", which is what QStyleHints reverts to when it is assigned.
    property int colorScheme: Qt.Unknown

    // Main.qml owns both the persisted property and the styleHints write, the
    // same division AboutDialog.qml uses for licensesRequested.
    signal colorSchemeSelected(int scheme)

    // The local folder standing in for the MEGA root, as a native path; empty
    // means nothing is linked. Owned and persisted by Main.qml like colorScheme.
    property string localRootFolder: ""

    // Empty path means "unlinked" -- one signal, so the two answers cannot drift
    // apart in the caller.
    signal localRootFolderSelected(string path)

    // Exposed for tst_MainDialogs.qml, like themeSelector above.
    property alias localFolderField: localFolderField

    // The ComboBox's own order. Index and scheme are kept apart because the
    // scheme values are not contiguous with the row order in general.
    readonly property var schemeOrder: [Qt.Unknown, Qt.Light, Qt.Dark]

    // Exposed for tst_MainDialogs.qml: the selection and the signal it triggers
    // are this file's only logic, and both live on the ComboBox.
    property alias themeSelector: themeSelector

    function indexOfScheme(scheme: int): int {
        const index = root.schemeOrder.indexOf(scheme);
        return index < 0 ? 0 : index;
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Settings")
    standardButtons: Dialog.Close

    // Read once per open rather than bound: ComboBox assigns currentIndex
    // imperatively when the user picks a row, which would break a binding here
    // for the rest of the session.
    onAboutToShow: themeSelector.currentIndex = root.indexOfScheme(root.colorScheme)

    GridLayout {
        columns: 2
        columnSpacing: Theme.spacing.lg
        rowSpacing: Theme.spacing.md

        Label {
            text: qsTr("Theme")
        }

        ComboBox {
            id: themeSelector
            Layout.minimumWidth: 200
            model: [qsTr("Use system setting"), qsTr("Light"), qsTr("Dark")]
            // activated, not currentIndexChanged: only a user pick may write the
            // preference, or the assignment in onAboutToShow above would echo
            // back as one.
            onActivated: index => root.colorSchemeSelected(root.schemeOrder[index])
        }

        Label {
            text: qsTr("Local folder")
        }

        RowLayout {
            spacing: Theme.spacing.sm

            TextField {
                id: localFolderField
                Layout.minimumWidth: 200
                Layout.fillWidth: true
                // Display only: the path is picked with the folder chooser, so
                // typing one would be a second, unvalidated way in.
                readOnly: true
                text: root.localRootFolder
                placeholderText: qsTr("Not linked")
            }

            Button {
                text: qsTr("Choose…")
                onClicked: folderChooser.open()
            }

            Button {
                text: qsTr("Clear")
                enabled: root.localRootFolder !== ""
                onClicked: root.localRootFolderSelected("")
            }
        }
    }

    // Nothing about the chosen folder is checked against the MEGA tree: the link
    // is a naming convention, and verifying it would mean walking both trees on
    // every change.
    FolderDialog {
        id: folderChooser
        title: qsTr("Choose the local folder for your MEGA root")
        onAccepted: root.localRootFolderSelected(
                        localFolderController.pathFromUrl(folderChooser.selectedFolder))
    }
}
