import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import MegaExplorer

// The in-app image viewer: a window of its own, showing a file's *original* bytes
// rather than the small server-generated JPEG the preview pane draws. The bytes
// arrive over the SDK's local HTTP server, so Image.source is just a URL and
// nothing is written to disk.
//
// One instance per opened image: Main.qml creates a window per double-click and
// destroys it when it hides, and sets transientParent so the window still belongs
// to the main one. Nothing here assumes it is the only viewer standing.
//
// controller is untyped `var` for PreviewPane.qml's reasons: a typed property
// would drag a views/ import into components/, and injecting the controller
// rather than reading the context property keeps the file loadable by the QML
// test harness, which installs no context properties.
Window {
    id: root

    required property var controller

    property var currentHandle: undefined
    property string currentName: ""
    property url source: ""

    // Fit-to-window is the default: the image is scaled to the window in both
    // directions, enlarging a small original as well as shrinking a large one.
    // Double-clicking switches to the true pixel size and back, with the Flickable
    // below supplying the panning that then becomes necessary.
    property bool actualSize: false

    // A function rather than an inline binding so the QML test can drive the fit
    // maths without a decoded image behind Image.implicitWidth.
    function fitScaleFor(imageWidth, imageHeight, boxWidth, boxHeight) {
        if (imageWidth <= 0 || imageHeight <= 0 || boxWidth <= 0 || boxHeight <= 0)
            return 1;
        return Math.min(boxWidth / imageWidth, boxHeight / imageHeight);
    }

    readonly property bool showing: root.visible
    readonly property bool failed: root.showing && (String(root.source) === "" || picture.status
                                                    === Image.Error)

    width: 960
    height: 640
    minimumWidth: 240
    minimumHeight: 180
    title: root.currentName
    // Its own dark ground in both themes, the way image viewers generally are: a
    // surface-coloured one tints the judgement of the image sitting on it.
    color: "#1c1c1c"

    function open(handle, name) {
        if (!root.controller || root.controller.viewerKind(name) !== "image")
            return;
        root.actualSize = false;
        root.currentHandle = handle;
        root.currentName = name;
        root.source = root.controller.sourceUrl(handle);
        root.show();
        root.raise();
        root.requestActivate();
    }

    // Hiding is the only teardown path -- the native close button, close() and a
    // plain visible = false all land here, so the image is released once for all
    // three.
    onVisibleChanged: {
        if (!root.visible)
            root.releaseImage();
    }

    function releaseImage() {
        root.currentHandle = undefined;
        root.currentName = "";
        // Cleared rather than left behind: an original can be tens of megabytes once
        // decoded, and Image holds it for as long as its source is set.
        root.source = "";
        root.actualSize = false;
    }

    Flickable {
        id: flick
        anchors.fill: parent
        // Focused so the Esc handler below is the one the keys reach.
        focus: true
        contentWidth: Math.max(width, picture.width)
        contentHeight: Math.max(height, picture.height)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        // Nothing to pan while the whole image fits.
        interactive: root.actualSize

        // Esc closes the window, off the focused item rather than a window-scoped
        // Shortcut: QWindow::isActive() is true for a transient child whenever its
        // parent is active, so with two viewers up both Shortcuts matched and
        // QShortcutMap dropped the key as ambiguous.
        Keys.onEscapePressed: root.close()

        Image {
            id: picture

            readonly property real fitScale: root.fitScaleFor(implicitWidth, implicitHeight,
                                                              flick.width, flick.height)

            source: root.source
            // Off: the cache is keyed by URL and would otherwise hold every original
            // opened this session, each at its full decoded size.
            cache: false
            asynchronous: true
            fillMode: Image.PreserveAspectFit
            smooth: true
            // Fit downscales a large original hard, where bilinear filtering alone
            // aliases badly on a photo.
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
}
