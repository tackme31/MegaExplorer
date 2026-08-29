import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts
import MegaExplorer

// The in-app PDF viewer: a window of its own, showing one page at a time off the
// SDK's local HTTP server. Unlike the image and video viewers it cannot hand the URL
// straight to a Qt type -- PdfPageItem fetches and renders, for the reasons in its
// header -- so everything below the page is page navigation rather than transport.
//
// One instance per opened file, created and destroyed by Main.qml exactly as the
// other two viewers are.
//
// controller is untyped `var` for ImageViewer.qml's reasons: a typed property would
// drag a views/ import into components/, and injecting the controller rather than
// reading the context property keeps the file loadable by the QML test harness,
// which installs no context properties.
Window {
    id: root

    required property var controller

    property var currentHandle: undefined
    property string currentName: ""
    property url source: ""

    readonly property bool showing: root.visible
    readonly property bool failed: root.showing && (String(root.source) === "" || page.status
                                                    === PdfPageItem.Error)

    width: 860
    height: 900
    minimumWidth: 320
    minimumHeight: 320
    title: root.currentName
    // A neutral grey ground rather than the theme's surface: a page is white paper,
    // and the margin around it has to read as "not the page".
    color: "#3a3a3a"

    function open(handle, name) {
        if (!root.controller || root.controller.viewerKind(name) !== "pdf")
            return;
        root.currentHandle = handle;
        root.currentName = name;
        root.source = root.controller.sourceUrl(handle);
        root.show();
        root.raise();
        root.requestActivate();
    }

    // Hiding is the only teardown path -- the native close button, close() and a
    // plain visible = false all land here, so the document is dropped once for all
    // three.
    onVisibleChanged: {
        if (!root.visible)
            root.releaseDocument();
    }

    function releaseDocument() {
        root.currentHandle = undefined;
        root.currentName = "";
        // Cleared rather than left behind: PdfPageItem holds the whole file in memory
        // for as long as its source is set.
        root.source = "";
    }

    function nextPage() {
        page.currentPage = page.currentPage + 1;
    }

    function previousPage() {
        page.currentPage = page.currentPage - 1;
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
        Keys.onLeftPressed: root.previousPage()
        Keys.onRightPressed: root.nextPage()
        Keys.onPressed: event => {
            if (event.key === Qt.Key_PageUp) {
                root.previousPage();
                event.accepted = true;
            } else if (event.key === Qt.Key_PageDown) {
                root.nextPage();
                event.accepted = true;
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            PdfPageItem {
                id: page

                anchors.fill: parent
                anchors.margins: Theme.spacing.md
                source: root.source
            }

            BusyIndicator {
                anchors.centerIn: parent
                running: root.showing && page.status === PdfPageItem.Loading
                visible: running
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.spacing.xl
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: "#ffffff"
                // The two failures kept apart: no URL means the local server would not
                // start, an item error means the bytes never became a document.
                text: String(root.source) === "" ? "This file could not be opened." :
                                                   "This PDF could not be displayed."
                visible: root.failed
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.rowHeight.toolbar
            // A ground of its own rather than the window's, matching the video
            // viewer's transport strip.
            color: "#2b2b2b"

            RowLayout {
                anchors.centerIn: parent
                spacing: Theme.spacing.md

                ToolButton {
                    id: previous

                    // Focus stays on the layout above, which carries the arrow keys;
                    // Space would otherwise re-trigger whichever button holds it.
                    focusPolicy: Qt.NoFocus
                    implicitWidth: 32
                    implicitHeight: 32
                    enabled: page.status === PdfPageItem.Ready && page.currentPage > 0
                    text: Theme.glyph.chevronLeft
                    font.family: Theme.font.iconFamily
                    // Spelled out rather than left to the style: the button sits on the
                    // dark strip above, where the style's own text colour is the one
                    // picked for a light surface.
                    contentItem: Text {
                        text: previous.text
                        font: previous.font
                        color: previous.enabled ? "#ffffff" : "#808080"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: root.previousPage()
                }

                Label {
                    text: page.pageCount > 0 ? (page.currentPage + 1) + " / " + page.pageCount : ""
                    color: "#ffffff"
                    font.pixelSize: Theme.font.caption
                    horizontalAlignment: Text.AlignHCenter
                    // Fixed so the strip does not jump as the page number widens.
                    Layout.minimumWidth: 72
                }

                ToolButton {
                    id: next

                    focusPolicy: Qt.NoFocus
                    implicitWidth: 32
                    implicitHeight: 32
                    enabled: page.status === PdfPageItem.Ready && page.currentPage < page.pageCount
                             - 1
                    text: Theme.glyph.chevronRight
                    font.family: Theme.font.iconFamily
                    contentItem: Text {
                        text: next.text
                        font: next.font
                        color: next.enabled ? "#ffffff" : "#808080"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: root.nextPage()
                }
            }
        }
    }
}
