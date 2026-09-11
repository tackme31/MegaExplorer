import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts
import MegaExplorer

// The in-app zip viewer: one window per archive, walked a folder at a time with a
// breadcrumb and an Up button rather than as an expanding tree
// (STUDY_ARCHIVE_EXTRACTION.md §1 A1-A2). The preview pane's indented listing is a
// separate thing and stays as it is.
//
// controller is untyped `var` for ImageViewer.qml's reasons: a typed property would
// drag a views/ import into components/, and injecting the controller rather than
// reading the context property keeps the file loadable by the QML test harness,
// which installs no context properties.
Window {
    id: root

    required property var controller
    // DownloadController, which extracts a file row into Downloads. Null leaves the
    // viewer browse-only.
    property var downloads: null

    property var currentHandle: undefined
    property string currentName: ""
    // An ArchiveBrowser parented to this window, so it goes when the window does.
    property var browser: null

    // The folder just left, so going up lands on it the way Explorer does rather
    // than back at the top of its parent.
    property string returnTo: ""

    readonly property bool showing: root.visible
    readonly property bool ready: root.browser !== null && root.browser.state
                                  === ArchiveBrowser.Ready
    readonly property bool loading: root.browser !== null && root.browser.state
                                    === ArchiveBrowser.Loading
    readonly property bool failed: root.browser !== null && root.browser.state
                                   === ArchiveBrowser.Failed
    readonly property bool canGoUp: root.ready && root.browser.path.length > 0
    readonly property var currentEntry: {
        const entries = root.ready ? root.browser.entries : [];
        const index = entryList.currentIndex;
        return index >= 0 && index < entries.length ? entries[index] : null;
    }
    readonly property bool canExtract: root.downloads !== null && root.currentEntry !== null &&
                                       !root.currentEntry.isDirectory
                                       && root.currentEntry.extractable === true
    // The archive itself stands for the root, so the trail is never empty.
    readonly property var crumbs: root.ready ? [root.currentName].concat(root.browser.path) :
                                               [root.currentName]

    width: 640
    height: 520
    minimumWidth: 360
    minimumHeight: 280
    title: root.currentName
    color: Theme.color.surface

    // forced: Open as chose this viewer, so the extension is not asked.
    function open(handle, name, sizeBytes, forced) {
        if (!root.controller || (forced !== true && root.controller.viewerKind(name) !== "archive"))
            return;
        root.currentHandle = handle;
        root.currentName = name;
        root.browser = root.controller.openArchive(handle, sizeBytes, root);
        root.show();
        root.raise();
        root.requestActivate();
    }

    onVisibleChanged: {
        if (!root.visible)
            root.releaseArchive();
    }

    function releaseArchive() {
        root.currentHandle = undefined;
        root.currentName = "";
        root.browser = null;
    }

    // A folder opens; a file is extracted, as a double-click on a file in the main
    // view downloads it.
    function openRow(index) {
        const entries = root.ready ? root.browser.entries : [];
        if (index < 0 || index >= entries.length)
            return;
        if (entries[index].isDirectory)
            root.browser.openFolder(entries[index].name);
        else
            root.extractRow(index);
    }

    function extractRow(index) {
        const entries = root.ready ? root.browser.entries : [];
        if (!root.downloads || index < 0 || index >= entries.length)
            return;
        const entry = entries[index];
        if (entry.isDirectory || entry.extractable !== true)
            return;
        root.downloads.extractArchiveEntry(root.browser, entry.name);
    }

    function blockedText(reason) {
        switch (reason) {
        case "encrypted":
            return qsTr("Encrypted — can't be extracted");
        case "unsupportedMethod":
            return qsTr("Compression method not supported — can't be extracted");
        default:
            return "";
        }
    }

    function goUp() {
        if (!root.canGoUp)
            return;
        const path = root.browser.path;
        root.returnTo = path[path.length - 1];
        root.browser.goUp();
    }

    function goToDepth(depth) {
        if (root.ready)
            root.browser.goToDepth(depth);
    }

    // Each folder starts on its first row, or on returnTo, rather than inheriting the
    // row and scroll position of the folder before it. Deferred because the lists'
    // model bindings hang off the same signal, and may not have re-run yet.
    function resetView() {
        const entries = root.ready ? root.browser.entries : [];
        const back = entries.findIndex(entry => entry.isDirectory && entry.name === root.returnTo);
        root.returnTo = "";
        entryList.currentIndex = back >= 0 ? back : (entries.length > 0 ? 0 : -1);
        if (back >= 0)
            entryList.positionViewAtIndex(back, ListView.Contain);
        else
            entryList.positionViewAtBeginning();
        crumbList.positionViewAtEnd();
    }

    Connections {
        target: root.browser
        function onChanged() {
            Qt.callLater(root.resetView);
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        // Focused so the key handlers below are the ones the keys reach, off the item
        // rather than window-scoped Shortcuts: QWindow::isActive() is true for a
        // transient child whenever its parent is active, so with two viewers up both
        // Shortcuts matched and QShortcutMap dropped the key as ambiguous.
        focus: true
        Keys.onEscapePressed: root.close()
        // Alt+Up is Explorer's Up; it arrives as Key_Up with the modifier set.
        Keys.onUpPressed: event => {
            if (event.modifiers & Qt.AltModifier)
                root.goUp();
            else
                entryList.decrementCurrentIndex();
        }
        Keys.onDownPressed: entryList.incrementCurrentIndex()
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                root.openRow(entryList.currentIndex);
                event.accepted = true;
            } else if (event.key === Qt.Key_Backspace) {
                root.goUp();
                event.accepted = true;
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.rowHeight.toolbar
            color: Theme.color.surfaceAlt

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing.md
                anchors.rightMargin: Theme.spacing.md
                spacing: Theme.spacing.sm

                ToolButton {
                    objectName: "upButton"
                    // Focus stays on the layout above, which carries the key handlers.
                    focusPolicy: Qt.NoFocus
                    implicitWidth: 32
                    implicitHeight: 32
                    Layout.alignment: Qt.AlignVCenter
                    enabled: root.canGoUp
                    text: Theme.glyph.up
                    font.family: Theme.font.iconFamily
                    onClicked: root.goUp()
                }

                ListView {
                    id: crumbList

                    objectName: "crumbList"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    orientation: ListView.Horizontal
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: root.crumbs

                    delegate: Row {
                        id: crumb

                        required property string modelData
                        required property int index

                        height: crumbList.height
                        spacing: Theme.spacing.xs

                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: crumb.index > 0
                            text: Theme.glyph.chevronRight
                            font.family: Theme.font.iconFamily
                            font.pixelSize: Theme.font.caption
                            color: Theme.color.textSecondary
                        }

                        ToolButton {
                            anchors.verticalCenter: parent.verticalCenter
                            focusPolicy: Qt.NoFocus
                            implicitHeight: 32
                            text: crumb.modelData
                            onClicked: root.goToDepth(crumb.index)
                        }
                    }
                }

                // The hover and the tooltip sit on this always-enabled wrapper: a disabled
                // button receives no hover, and a blocked row is when the tooltip matters.
                Item {
                    implicitWidth: extractButton.implicitWidth
                    implicitHeight: extractButton.implicitHeight
                    Layout.alignment: Qt.AlignVCenter
                    visible: root.downloads !== null

                    ToolButton {
                        id: extractButton

                        objectName: "extractButton"
                        anchors.fill: parent
                        focusPolicy: Qt.NoFocus
                        implicitWidth: 32
                        implicitHeight: 32
                        enabled: root.canExtract
                        text: Theme.glyph.transferDown
                        font.family: Theme.font.iconFamily
                        onClicked: root.extractRow(entryList.currentIndex)
                    }

                    HoverHandler {
                        id: extractHover
                    }
                    ToolTip.visible: extractHover.hovered
                    ToolTip.delay: 500
                    ToolTip.text: {
                        const entry = root.currentEntry;
                        if (entry !== null && !entry.isDirectory && entry.extractable !== true)
                            return root.blockedText(entry.blockedReason);
                        return qsTr("Extract to Downloads");
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.border.thin
                color: Theme.color.stroke
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: entryList

                objectName: "entryList"
                anchors.fill: parent
                anchors.margins: Theme.spacing.sm
                clip: true
                visible: root.ready
                model: root.ready ? root.browser.entries : []
                boundsBehavior: Flickable.StopAtBounds
                // The layout above owns the keys; this only follows currentIndex.
                keyNavigationEnabled: false
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    id: entryRow

                    required property var modelData
                    required property int index

                    width: entryList.width
                    height: Theme.rowHeight.normal
                    radius: Theme.radius.sm
                    color: entryRow.ListView.isCurrentItem ? Theme.color.selection :
                                                             rowHover.hovered
                                                             ? Theme.color.subtleHover :
                                                               "transparent"

                    HoverHandler {
                        id: rowHover
                    }

                    TapHandler {
                        onTapped: entryList.currentIndex = entryRow.index
                        onDoubleTapped: root.openRow(entryRow.index)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: Theme.spacing.md
                        anchors.rightMargin: Theme.spacing.md
                        spacing: Theme.spacing.md

                        FileIcon {
                            Layout.alignment: Qt.AlignVCenter
                            isFolder: entryRow.modelData.isDirectory
                            fileName: entryRow.modelData.name
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                            elide: Text.ElideMiddle
                            font.pixelSize: Theme.font.body
                            text: entryRow.modelData.name
                        }

                        Label {
                            objectName: "blockedLabel"
                            Layout.alignment: Qt.AlignVCenter
                            visible: !entryRow.modelData.isDirectory
                                     && entryRow.modelData.extractable === false
                            color: Theme.color.textSecondary
                            font.pixelSize: Theme.font.caption
                            text: entryRow.modelData.blockedReason === "encrypted"
                                  ? qsTr("Encrypted") : qsTr("Unsupported compression")
                        }

                        Label {
                            Layout.alignment: Qt.AlignVCenter
                            color: Theme.color.textSecondary
                            font.pixelSize: Theme.font.caption
                            text: entryRow.modelData.formattedSize
                        }
                    }
                }
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: root.showing && root.loading
                visible: running
            }

            Label {
                objectName: "statusLabel"
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.spacing.xl
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.color.textSecondary
                visible: root.failed || (root.ready && entryList.count === 0)
                text: {
                    if (root.ready)
                        return qsTr("This folder is empty");
                    if (!root.failed)
                        return "";
                    switch (root.browser.reason) {
                    case ArchiveBrowser.Empty:
                        return qsTr("This archive is empty");
                    case ArchiveBrowser.FetchFailed:
                        return qsTr("This file could not be loaded.");
                    default:
                        return qsTr(
                                    "This file could not be read as a zip archive. It may be a different format, or damaged.");
                    }
                }
            }
        }
    }
}
