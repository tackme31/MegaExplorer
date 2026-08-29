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
// Nested inside Main.qml's ApplicationWindow, which makes it transient for the
// main window: it stays above it and goes away with it, and the app does not
// outlive the main window because a viewer was left open.
//
// One window, reused: opening another image replaces what this one shows.
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

    // Fit-to-window is the default and never enlarges past 1:1, so a small image is
    // never blurred; double-clicking switches to the true pixel size and back, with
    // the Flickable below supplying the panning that then becomes necessary.
    property bool actualSize: false

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
        if (!root.controller || !root.controller.canView(name))
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

    // Not Keys.onEscapePressed: a Window is not an Item and holds no focus of its
    // own, so the handler would need a focused item under it. A window-scoped
    // Shortcut fires whenever this window is the active one.
    Shortcut {
        sequences: [StandardKey.Cancel]
        onActivated: root.close()
    }

    Flickable {
        id: flick
        anchors.fill: parent
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
