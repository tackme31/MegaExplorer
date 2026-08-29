import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import MegaExplorer

// The in-app image viewer: one per window, laid over the whole content area, showing
// a file's *original* bytes rather than the small server-generated JPEG the preview
// pane draws. The bytes arrive over the SDK's local HTTP server, so Image.source is
// just a URL and nothing is written to disk.
//
// controller and listModel are untyped `var` for PreviewPane.qml's reasons: a typed
// property would drag a views/ import into components/, and injecting the controller
// rather than reading the context property keeps the file loadable by the QML test
// harness, which installs no context properties.
Item {
    id: root

    required property var controller

    // The FileListModel the shown row belongs to. Held from open() to close() so the
    // previous/next walk stays in the folder the viewer was opened from; a navigation
    // underneath the viewer shifts the rows, which is why closing drops it.
    property var listModel: null
    property int currentRow: -1
    // Kept alongside the row because the row alone goes stale: the address bar and
    // breadcrumb sit in the window's header, outside the area this covers, so the
    // listing can be replaced under an open viewer. step() re-resolves from this.
    property var currentHandle: undefined
    property string currentName: ""
    property url source: ""

    // Fit-to-window is the default and never enlarges past 1:1, so a small image is
    // never blurred; double-clicking switches to the true pixel size and back, with
    // the Flickable below supplying the panning that then becomes necessary.
    property bool actualSize: false

    readonly property bool showing: root.currentRow >= 0
    readonly property bool failed: root.showing && (String(root.source) === "" || picture.status
                                                    === Image.Error)

    // Main.qml hands focus back to the file view on this -- an invisible item cannot
    // hold activeFocus, so the arrow keys would go dead after a close otherwise.
    signal closed

    visible: root.showing
    enabled: root.showing
    focus: root.showing

    function open(model, handle, name) {
        if (!root.controller || !model || !root.controller.canView(name))
            return;
        const row = model.rowForHandle(handle);
        if (row < 0)
            return;
        root.listModel = model;
        root.showRow(row, handle, name);
    }

    function showRow(row, handle, name) {
        root.actualSize = false;
        root.currentRow = row;
        root.currentHandle = handle;
        root.currentName = name;
        root.source = root.controller.sourceUrl(handle);
        // Deferred, not called straight: the double-click that opens the viewer is
        // still being delivered, and the file view's own tap handler takes focus
        // back on that same release (FileViewInput.handleLeftTap).
        Qt.callLater(root.takeFocus);
    }

    function takeFocus() {
        if (root.showing)
            root.forceActiveFocus();
    }

    // Walks outwards a row at a time rather than collecting every viewable row up
    // front: entryAt() builds a map per call, and the neighbour is nearly always
    // adjacent.
    function step(delta) {
        if (!root.showing || !root.listModel)
            return;
        // Not root.currentRow: a navigation from the address bar above refills the
        // same model, and walking from the old index would leave the folder on
        // screen. A handle that is gone means this listing no longer holds it.
        const from = root.listModel.rowForHandle(root.currentHandle);
        if (from < 0) {
            root.close();
            return;
        }
        root.currentRow = from;
        for (let row = from + delta; row >= 0 && row < root.listModel.count; row += delta) {
            const entry = root.listModel.entryAt(row);
            if (!entry || entry.name === undefined || entry.isFolder)
                continue;
            if (root.controller.canView(entry.name)) {
                root.showRow(row, entry.handle, entry.name);
                return;
            }
        }
    }

    function close() {
        if (!root.showing)
            return;
        root.currentRow = -1;
        root.currentHandle = undefined;
        root.currentName = "";
        // Cleared rather than left behind: an original can be tens of megabytes once
        // decoded, and Image holds it for as long as its source is set.
        root.source = "";
        root.listModel = null;
        root.closed();
    }

    Keys.onEscapePressed: root.close()
    Keys.onLeftPressed: root.step(-1)
    Keys.onRightPressed: root.step(1)

    // Its own dark ground in both themes, the way image viewers generally are: a
    // surface-coloured one tints the judgement of the image sitting on it.
    Rectangle {
        anchors.fill: parent
        color: "#1c1c1c"
    }

    // Swallows the mouse and wheel events the chrome and the picture don't take:
    // a Flickable ignores both while `interactive` is false, and a plain Rectangle
    // never accepted any. This stops item-level delivery only -- what makes the
    // content underneath inert is Main.qml disabling it (see the Loader there).
    // Declared before the Flickable so it stays underneath: panning and the
    // picture's own double-click still win.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        onWheel: wheel => wheel.accepted = true
    }

    // A MouseArea does not see drags, and this file cannot rely on its host having
    // disabled what is underneath: without a DropArea, a file dropped on the open
    // viewer uploads into a folder the user can no longer see. A bare one accepts,
    // which ends the search for a handler, and dropping on it does nothing.
    DropArea {
        anchors.fill: parent
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.topMargin: topBar.height
        contentWidth: Math.max(width, picture.width)
        contentHeight: Math.max(height, picture.height)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        // Nothing to pan while the whole image fits.
        interactive: root.actualSize

        Image {
            id: picture

            // Never above 1:1 -- blowing an image up to fill the window is not what
            // "fit" means here.
            readonly property real fitScale: (implicitWidth > 0 && implicitHeight > 0) ? Math.min(1,
                                                                                                  flick.width
                                                                                                  / implicitWidth,
                                                                                                  flick.height
                                                                                                  / implicitHeight) :
                                                                                         1

            source: root.source
            // Off: the cache is keyed by URL and would otherwise hold every original
            // opened this session, each at its full decoded size.
            cache: false
            asynchronous: true
            fillMode: Image.PreserveAspectFit
            smooth: true
            // Fit is the default view and it downscales the original hard, where
            // bilinear filtering alone aliases badly on a photo.
            mipmap: true
            width: implicitWidth * (root.actualSize ? 1 : picture.fitScale)
            height: implicitHeight * (root.actualSize ? 1 : picture.fitScale)
            x: Math.max(0, (flick.contentWidth - width) / 2)
            y: Math.max(0, (flick.contentHeight - height) / 2)

            MouseArea {
                anchors.fill: parent
                // A MouseArea inside a Flickable still lets a drag through -- the
                // Flickable filters child mouse events once the drag threshold is
                // passed -- so this costs no panning.
                onDoubleClicked: root.actualSize = !root.actualSize
            }
        }
    }

    // Centred in flick, not in the root: the root is taller by topBar.height, so
    // anchoring to it would put both a half-bar above the picture's own centre.
    BusyIndicator {
        anchors.centerIn: flick
        running: root.showing && picture.status === Image.Loading
        visible: running
    }

    Label {
        anchors.centerIn: flick
        width: flick.width - 2 * Theme.spacing.xl
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: "#ffffff"
        // The two failures kept apart: no URL means the local server would not start,
        // an Image error means the bytes did not decode.
        text: String(root.source) === "" ? "This file could not be opened." :
                                           "This image could not be displayed."
        visible: root.failed
    }

    // Chrome, declared after the picture so it paints over it.
    Rectangle {
        id: topBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Theme.rowHeight.caption
        color: Qt.rgba(0, 0, 0, 0.6)

        Label {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing.lg
            anchors.right: closeButton.left
            anchors.rightMargin: Theme.spacing.md
            anchors.verticalCenter: parent.verticalCenter
            text: root.currentName
            elide: Text.ElideMiddle
            color: "#ffffff"
        }

        ToolButton {
            id: closeButton
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: parent.height
            height: parent.height
            // NoFocus on all three buttons: a click that moved focus off the root
            // item would leave Escape and the arrow keys dead.
            focusPolicy: Qt.NoFocus
            font.family: Theme.font.iconFamily
            text: Theme.glyph.close
            onClicked: root.close()

            contentItem: Text {
                text: closeButton.text
                font: closeButton.font
                color: "#ffffff"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    ToolButton {
        id: previousButton
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.rowHeight.caption
        height: Theme.rowHeight.caption * 2
        focusPolicy: Qt.NoFocus
        font.family: Theme.font.iconFamily
        text: Theme.glyph.chevronLeft
        onClicked: root.step(-1)

        contentItem: Text {
            text: previousButton.text
            font: previousButton.font
            color: "#ffffff"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    ToolButton {
        id: nextButton
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.rowHeight.caption
        height: Theme.rowHeight.caption * 2
        focusPolicy: Qt.NoFocus
        font.family: Theme.font.iconFamily
        text: Theme.glyph.chevronRight
        onClicked: root.step(1)

        contentItem: Text {
            text: nextButton.text
            font: nextButton.font
            color: "#ffffff"
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
