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

    // The thumbnail cache's size as already-formatted text, empty when there is no
    // answer to show. Measured and emptied by Main.qml's controller, so this file
    // stays free of root-context lookups like the properties above.
    property string cacheSizeText: ""
    property bool cacheBusy: false

    // Reading the size enumerates a directory, so it is asked for on open rather
    // than held: a signal, because a second onAboutToShow declared at the
    // instantiation site would replace this file's own.
    signal cacheSizeRequested
    signal cacheClearRequested

    // Exposed for tst_MainDialogs.qml: the three-way wording below is this file's
    // only decision about the cache.
    property alias cacheSizeLabel: cacheSizeLabel

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
    Component.onCompleted: StandardButtonLabels.pin(footer)

    // Fixed rather than content-sized: the pages hold two cards at most, and a
    // dialog that hugs them leaves the category list beside it a stub.
    width: Math.min(Overlay.overlay.width * 0.9, 680)
    height: Math.min(Overlay.overlay.height * 0.85, 360)

    // One setting per card, the way Windows 11's Settings app draws one. The
    // box is what separates a setting's name from its control -- stacked in a
    // plain column the two read as consecutive rows of one list. The name stays
    // above the control rather than opposite it: the dialog is not wide enough
    // for a name column plus a path field and its two buttons.
    component SettingCard: Rectangle {
        default property alias content: cardContent.data
        property alias name: cardName.text

        Layout.fillWidth: true
        implicitHeight: cardContent.implicitHeight + Theme.spacing.lg * 2
        color: Theme.color.surfaceAlt
        border.color: Theme.color.stroke
        border.width: Theme.border.thin
        radius: Theme.radius.md

        ColumnLayout {
            id: cardContent

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.spacing.lg
            anchors.rightMargin: Theme.spacing.lg
            spacing: Theme.spacing.sm

            Label {
                id: cardName
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }
    }

    // Read once per open rather than bound: ComboBox assigns currentIndex
    // imperatively when the user picks a row, which would break a binding here
    // for the rest of the session.
    onAboutToShow: {
        themeSelector.currentIndex = root.indexOfScheme(root.colorScheme);
        root.cacheSizeRequested();
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacing.md

        // Same master/detail split as LicenseDialog: categories on the left,
        // the selected one's settings on the right. StackLayout (not a Loader)
        // keeps every page's controls alive even when hidden, so tests can
        // reach e.g. cacheSizeLabel without switching to File management first.
        ListView {
            id: categoryList

            Layout.preferredWidth: 150
            Layout.fillHeight: true
            clip: true
            model: [qsTr("General"), qsTr("File management")]
            currentIndex: 0
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: categoryRow

                required property int index
                required property string modelData

                width: categoryList.width
                text: modelData
                highlighted: ListView.isCurrentItem
                onClicked: categoryList.currentIndex = categoryRow.index
            }
        }

        Rectangle {
            Layout.preferredWidth: Theme.border.thin
            Layout.fillHeight: true
            color: Theme.color.stroke
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: Theme.spacing.sm
            currentIndex: categoryList.currentIndex

            ColumnLayout {
                spacing: Theme.spacing.md

                SettingCard {
                    name: qsTr("Theme")

                    ComboBox {
                        id: themeSelector
                        Layout.fillWidth: true
                        Layout.maximumWidth: 260
                        model: [qsTr("Use system setting"), qsTr("Light"), qsTr("Dark")]
                        // activated, not currentIndexChanged: only a user pick may
                        // write the preference, or the assignment in onAboutToShow
                        // above would echo back as one.
                        onActivated: index => root.colorSchemeSelected(root.schemeOrder[index])
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }

            ColumnLayout {
                spacing: Theme.spacing.md

                SettingCard {
                    name: qsTr("Local folder")

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing.sm

                        TextField {
                            id: localFolderField
                            Layout.fillWidth: true
                            // Display only: the path is picked with the folder
                            // chooser, so typing one would be a second,
                            // unvalidated way in.
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

                SettingCard {
                    name: qsTr("Thumbnail cache")

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing.sm

                        // No cap and no automatic eviction: the size is shown so
                        // the user can decide instead (STUDY_THUMBNAIL_CACHE.md 2-4).
                        Label {
                            id: cacheSizeLabel
                            Layout.fillWidth: true
                            text: root.cacheBusy ? qsTr("Calculating…") : (root.cacheSizeText
                                                                           || qsTr("Unavailable"))
                        }

                        Button {
                            text: qsTr("Clear")
                            enabled: !root.cacheBusy
                            onClicked: root.cacheClearRequested()
                        }
                    }
                }

                Item {
                    Layout.fillHeight: true
                }
            }
        }
    }

    // Nothing about the chosen folder is checked against the MEGA tree: the link
    // is a naming convention, and verifying it would mean walking both trees on
    // every change.
    FolderDialog {
        id: folderChooser
        title: qsTr("Choose the local folder for your MEGA root")
        onAccepted: root.localRootFolderSelected(localFolderController.pathFromUrl(
                                                     folderChooser.selectedFolder))
    }
}
