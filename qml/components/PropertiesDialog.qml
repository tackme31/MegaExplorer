import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml/AboutDialog.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts

// Explorer's "Properties" for one MEGA node: what the row already showed, plus
// the two answers only a lookup can give -- where it lives, and what a folder
// holds. One instance for the whole app, in Main.qml, wired to its controller's
// signal the way MissingPinDialog is.
Dialog {
    id: root

    required property var properties

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Properties")
    standardButtons: Dialog.Close

    // Same cap as MissingPinDialog: a Popup takes its content's *implicit* width,
    // and a long name or path would otherwise drag the frame past the window edge.
    readonly property real maxWidth: Overlay.overlay.width - 48
    width: Math.min(implicitWidth, maxWidth)

    readonly property real valueWidth: Math.min(360, root.maxWidth - 160)

    // Must stay identical to FileTableView's modifiedDateFormat -- qsTr's context is
    // the QML type, so a .ts file translates the two copies separately.
    readonly property string modifiedDateFormat: qsTr("M/d/yyyy h:mm AP")

    // Root label plus the folder chain under it, e.g. "Cloud Drive/Photos".
    // ViewLabels rather than a string in C++: naming a screen the app invented is
    // QML's half of the split (see ViewLabels.qml).
    readonly property string locationText: ViewLabels.label(root.properties.rootKind, true, "")
                                           + root.properties.parentPath

    // "" once the lookup has landed, so every row below reads
    // `pendingText() || <the real value>`. Written as a function rather than three
    // inline conditionals because the formatter turns those into unreadable
    // ternary ladders; a binding still re-evaluates, since QML tracks property
    // reads inside the functions a binding calls.
    function pendingText(): string {
        if (root.properties.loading)
            return qsTr("Loading…");
        if (root.properties.failed)
            return qsTr("Unavailable");
        return "";
    }

    function sizeText(): string {
        // Under one KiB formattedSize is already the exact byte count (the
        // formatter is 1024-based), so the bracketed repeat would say the same
        // number twice.
        if (root.properties.sizeBytes < 1024)
            return root.properties.formattedSize;
        // A named locale, not Qt.locale(): the controller already words the unit in
        // English, so grouping the exact count by the system's rules would mix two
        // conventions in one line.
        return qsTr("%1 (%2 bytes)").arg(root.properties.formattedSize).arg(Number(
                                                                                root.properties.sizeBytes).toLocaleString(
                                                                                Qt.locale("en_US"),
                                                                                "f", 0));
    }

    function modifiedText(): string {
        if (root.properties.modificationTime <= 0)
            return qsTr("Unknown");
        return Qt.formatDateTime(new Date(root.properties.modificationTime * 1000),
                                 root.modifiedDateFormat);
    }

    GridLayout {
        columns: 2
        columnSpacing: Theme.spacing.lg
        rowSpacing: Theme.spacing.md

        Label {
            text: qsTr("Name")
            color: Theme.color.textSecondary
        }
        Label {
            Layout.maximumWidth: root.valueWidth
            wrapMode: Text.Wrap
            text: root.properties.name
        }

        Label {
            text: qsTr("Type")
            color: Theme.color.textSecondary
        }
        Label {
            text: root.properties.isFolder ? qsTr("Folder") : qsTr("File")
        }

        Label {
            text: qsTr("Location")
            color: Theme.color.textSecondary
        }
        Label {
            Layout.maximumWidth: root.valueWidth
            wrapMode: Text.Wrap
            text: root.pendingText() || root.locationText
        }

        Label {
            text: qsTr("Size")
            color: Theme.color.textSecondary
        }
        Label {
            // A folder's total arrives with the lookup, a file's came in with the
            // row -- so only the folder case has a pending state to show.
            text: root.properties.isFolder ? (root.pendingText() || root.sizeText()) : root.sizeText(
                                                 )
        }

        Label {
            text: qsTr("Contains")
            color: Theme.color.textSecondary
            visible: root.properties.isFolder
        }
        Label {
            visible: root.properties.isFolder
            text: root.pendingText() || qsTr("%1 files, %2 folders").arg(
                      root.properties.fileCount).arg(root.properties.folderCount)
        }

        Label {
            text: qsTr("Modified")
            color: Theme.color.textSecondary
            // Files only: MEGA stamps a folder's time on creation, so the listing
            // leaves the column blank for one too.
            visible: !root.properties.isFolder
        }
        Label {
            visible: !root.properties.isFolder
            text: root.modifiedText()
        }
    }

    Connections {
        target: root.properties
        function onShowRequested() {
            root.open();
        }
    }
}
