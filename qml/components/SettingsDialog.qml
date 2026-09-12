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

    // OpenWithController, or a stand-in with its entries/setEntries/commandRunnable/
    // commandWithProgram surface in tests. Passed in, like everything above.
    property var openWith: null

    // Exposed for tst_MainDialogs.qml.
    property alias programList: programList
    property alias programNameField: programNameField
    property alias programExtensionsField: programExtensionsField
    property alias programCommandField: programCommandField
    property alias programProblemLabel: programProblemLabel

    // The command the inline warning was last computed for. Trails the field by a
    // debounce: the check stats every PATH directory, and a network drive on PATH
    // would make that stall per keystroke.
    property string checkedCommand: ""

    // The editable copy of the list, rows included that setEntries will not keep yet
    // (no name or no command): writing each keystroke straight to the controller and
    // reading it back would delete the row being typed into.
    ListModel {
        id: programModel
    }

    function loadPrograms() {
        programModel.clear();
        const entries = root.openWith ? root.openWith.entries() : [];
        for (let i = 0; i < entries.length; ++i)
            programModel.append({
                                    "name": entries[i].name,
                                    "extensions": entries[i].extensions,
                                    "commandLine": entries[i].commandLine
                                });
        programList.currentIndex = programModel.count > 0 ? 0 : -1;
        root.showSelectedProgram();
    }

    function savePrograms() {
        if (!root.openWith)
            return;
        let entries = [];
        for (let i = 0; i < programModel.count; ++i) {
            const row = programModel.get(i);
            entries.push({
                             "name": row.name,
                             "extensions": row.extensions,
                             "commandLine": row.commandLine
                         });
        }
        root.openWith.setEntries(entries);
    }

    // Imperative both ways: the fields' textEdited writes back, and a binding from
    // the model would be broken by the first keystroke.
    function showSelectedProgram() {
        const index = programList.currentIndex;
        const row = index >= 0 ? programModel.get(index) : null;
        programNameField.text = row ? row.name : "";
        programExtensionsField.text = row ? row.extensions : "";
        programCommandField.text = row ? row.commandLine : "";
        commandCheckDelay.stop();
        root.checkedCommand = programCommandField.text;
    }

    function editProgram(role: string, value: string) {
        const index = programList.currentIndex;
        if (index < 0 || programModel.get(index)[role] === value)
            return;
        programModel.setProperty(index, role, value);
        root.savePrograms();
    }

    function addProgram() {
        programModel.append({
                                "name": "",
                                "extensions": "",
                                "commandLine": ""
                            });
        programList.currentIndex = programModel.count - 1;
        root.showSelectedProgram();
        programNameField.forceActiveFocus();
    }

    function removeProgram() {
        const index = programList.currentIndex;
        if (index < 0)
            return;
        programModel.remove(index);
        programList.currentIndex = Math.min(index, programModel.count - 1);
        root.showSelectedProgram();
        root.savePrograms();
    }

    // Empty when there is nothing to warn about. Only the first problem is named,
    // since the line has room for one.
    readonly property string programProblem: {
        if (programList.currentIndex < 0)
            return "";
        if (root.checkedCommand.trim() === "")
            return qsTr("Enter the command line that starts the program.");
        if (root.openWith && !root.openWith.commandRunnable(root.checkedCommand))
            return qsTr("The program in this command line could not be found.");
        // Exactly empty, not blank: that is the test setEntries drops a row by.
        if (programNameField.text === "")
            return qsTr("Give the program a name. It is not saved without one.");
        return "";
    }

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
    // Tall enough for the Open with page's list, buttons and form; the other pages
    // take the slack in their trailing filler rather than each getting a height.
    width: Math.min(Overlay.overlay.width * 0.9, 680)
    height: Math.min(Overlay.overlay.height * 0.85, 480)

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
        root.loadPrograms();
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
            model: [qsTr("General"), qsTr("File management"), qsTr("Open with")]
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
                            // Reserved whether or not the button is showing, so
                            // a long path does not reflow when it comes and goes.
                            rightPadding: clearLocalFolder.implicitWidth + Theme.spacing.md * 2

                            // Hand-built rather than Qt 6.10's SearchField, which
                            // draws this button itself: that control has neither
                            // placeholderText nor readOnly, so it cannot stand in
                            // for a display-only field.
                            ToolButton {
                                id: clearLocalFolder

                                anchors.right: parent.right
                                anchors.rightMargin: Theme.spacing.md
                                anchors.verticalCenter: parent.verticalCenter
                                visible: root.localRootFolder !== ""
                                // Same squeeze as TabStrip's tab-close button:
                                // Fluent's icon-only ToolButton is 38x32, and the
                                // padding alone cannot get under the background's
                                // implicit 32, so the background is replaced too.
                                topPadding: Theme.spacing.xs
                                bottomPadding: Theme.spacing.xs
                                leftPadding: Theme.spacing.xs
                                rightPadding: Theme.spacing.xs
                                implicitWidth: 20
                                implicitHeight: 20
                                background: Rectangle {
                                    radius: Theme.radius.sm
                                    color: clearLocalFolder.pressed ? Theme.color.subtlePressed :
                                                                      clearLocalFolder.hovered
                                                                      ? Theme.color.subtleHover :
                                                                        "transparent"
                                }
                                font.family: Theme.font.iconFamily
                                font.pixelSize: 10
                                text: Theme.glyph.close
                                ToolTip.delay: 500
                                ToolTip.visible: clearLocalFolder.hovered
                                ToolTip.text: qsTr("Clear")
                                focusPolicy: Qt.NoFocus
                                onClicked: root.localRootFolderSelected("")
                            }
                        }

                        Button {
                            text: qsTr("Choose…")
                            onClicked: folderChooser.open()
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

            ColumnLayout {
                spacing: Theme.spacing.md

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.sm

                    // Without this the URL-only limit reads as a broken entry the
                    // first time a path-only program opens nothing (STUDY_OPEN_WITH.md 4-4).
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.font.caption
                        color: Theme.color.textSecondary
                        text: qsTr(
                                  "Files are handed over as a streaming URL, so only programs that accept a URL as an argument can open them.")
                    }

                    Button {
                        text: qsTr("Add")
                        onClicked: root.addProgram()
                    }

                    Button {
                        text: qsTr("Remove")
                        enabled: programList.currentIndex >= 0
                        onClicked: root.removeProgram()
                    }
                }

                // One framed box with dividers between rows, not a card per row: the
                // gap between cards would cost a visible row (STUDY_OPEN_WITH.md 3-3-4).
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 96
                    color: Theme.color.surfaceAlt
                    border.color: Theme.color.stroke
                    border.width: Theme.border.thin
                    radius: Theme.radius.md
                    clip: true

                    ListView {
                        id: programList

                        anchors.fill: parent
                        anchors.margins: Theme.border.thin
                        clip: true
                        model: programModel
                        currentIndex: -1
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        onCurrentIndexChanged: root.showSelectedProgram()

                        delegate: Rectangle {
                            id: programRow

                            required property int index
                            required property string name
                            required property string extensions
                            required property string commandLine

                            width: ListView.view.width
                            implicitHeight: programText.implicitHeight + Theme.spacing.sm * 2
                            color: ListView.isCurrentItem ? Theme.color.selection :
                                                            programHover.hovered
                                                            ? Theme.color.subtleHover :
                                                              "transparent"

                            HoverHandler {
                                id: programHover
                            }

                            TapHandler {
                                onTapped: programList.currentIndex = programRow.index
                            }

                            ColumnLayout {
                                id: programText

                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: Theme.spacing.lg
                                anchors.rightMargin: Theme.spacing.lg
                                spacing: 0

                                Label {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    font.pixelSize: Theme.font.body
                                    font.bold: true
                                    color: Theme.color.text
                                    text: programRow.name.trim() !== "" ? programRow.name : qsTr(
                                                                              "Untitled")
                                }

                                Label {
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    font.pixelSize: Theme.font.caption
                                    color: Theme.color.textSecondary
                                    text: programRow.extensions.trim() !== ""
                                          ? programRow.extensions : qsTr("All files")
                                }

                                // Monospace on this line only: on the extensions too it
                                // would flatten the step down from the bold name.
                                Label {
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                    font.pixelSize: Theme.font.caption
                                    font.family: Theme.font.monoFamily
                                    color: Theme.color.textSecondary
                                    text: programRow.commandLine
                                }
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: Theme.border.thin
                                color: Theme.color.stroke
                                visible: programRow.index < programModel.count - 1
                            }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: programModel.count === 0
                        color: Theme.color.textSecondary
                        text: qsTr("No programs yet")
                    }
                }

                // Live editor for the selected row: every edit is written through at
                // once, like the rest of this dialog, which has no Apply.
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: Theme.spacing.md
                    rowSpacing: Theme.spacing.sm
                    enabled: programList.currentIndex >= 0

                    Label {
                        text: qsTr("Name")
                    }

                    TextField {
                        id: programNameField
                        Layout.fillWidth: true
                        onTextEdited: root.editProgram("name", text)
                    }

                    Label {
                        text: qsTr("File types")
                    }

                    TextField {
                        id: programExtensionsField
                        Layout.fillWidth: true
                        placeholderText: qsTr("e.g. mp4, mkv (empty for all files)")
                        onTextEdited: root.editProgram("extensions", text)
                    }

                    Label {
                        text: qsTr("Command line")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing.sm

                        TextField {
                            id: programCommandField
                            Layout.fillWidth: true
                            font.family: Theme.font.monoFamily
                            placeholderText: qsTr("\"C:\\Path\\program.exe\" %U")
                            onTextEdited: {
                                root.editProgram("commandLine", text);
                                commandCheckDelay.restart();
                            }
                        }

                        Button {
                            text: qsTr("Browse…")
                            onClicked: programChooser.open()
                        }
                    }

                    Item {
                        Layout.preferredWidth: 1
                    }

                    // Kept in the layout while empty, so the list above does not
                    // resize as the warning comes and goes.
                    Label {
                        id: programProblemLabel
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: Theme.font.caption
                        color: Theme.color.danger
                        text: root.programProblem
                    }
                }
            }
        }
    }

    Timer {
        id: commandCheckDelay
        interval: 300
        onTriggered: root.checkedCommand = programCommandField.text
    }

    // The URL placeholder is kept, and on an empty command added: the chooser is
    // how most people will fill this in, and a bare path would work but hide %U.
    FileDialog {
        id: programChooser
        title: qsTr("Choose a program")
        nameFilters: [qsTr("Programs (*.exe)"), qsTr("All files (*)")]
        onAccepted: {
            if (!root.openWith)
                return;
            const command = root.openWith.commandWithProgram(programCommandField.text,
                                                             programChooser.selectedFile);
            programCommandField.text = command;
            root.editProgram("commandLine", command);
            commandCheckDelay.stop();
            root.checkedCommand = command;
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
