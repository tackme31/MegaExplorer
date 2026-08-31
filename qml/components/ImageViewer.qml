import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts
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

    // The images this window can step through: row-ordered {handle, name} maps taken
    // from the listing when it opened. A snapshot rather than the live model -- the
    // window outlives the tab it came from, and a model held across that is a
    // destroyed object (STUDY_VIEWER_SEPARATE_WINDOW 4-2-1). Going stale is the whole
    // cost, and this window is "these images", not the tab's current position.
    property var sequence: []
    property int sequenceIndex: -1

    readonly property bool canGoPrevious: root.sequenceIndex > 0
    readonly property bool canGoNext: root.sequenceIndex >= 0
                                      && root.sequenceIndex < root.sequence.length - 1

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

    // sequence is optional: without one the window shows this single image and the
    // strip below stays hidden.
    function open(handle, name, entries) {
        if (!root.controller || root.controller.viewerKind(name) !== "image")
            return;
        root.sequence = entries ?? [];
        root.sequenceIndex = root.indexOfHandle(handle);
        root.showImage(handle, name);
        root.show();
        root.raise();
        root.requestActivate();
    }

    function indexOfHandle(handle) {
        for (let i = 0; i < root.sequence.length; ++i) {
            if (root.sequence[i].handle === handle)
                return i;
        }
        return -1;
    }

    function showImage(handle, name) {
        root.actualSize = false;
        root.currentHandle = handle;
        root.currentName = name;
        root.source = root.controller.sourceUrl(handle);
        // Reloading the image drops the keyboard away from this window -- measured:
        // without this the arrow keys move once and every press after that lands in
        // the window underneath. Deferred because the focus goes only once the
        // reload has been processed. The overlay viewer this file grew out of
        // carried the same call for the double-click that opened it.
        Qt.callLater(root.takeFocus);
    }

    function takeFocus() {
        if (!root.visible)
            return;
        root.requestActivate();
        flick.forceActiveFocus();
    }

    // Clamped, not wrapping -- the same choice FileListModel::moveCursor makes, so
    // the ends of the listing stay ends here too.
    function step(delta) {
        const target = root.sequenceIndex + delta;
        if (root.sequenceIndex < 0 || target < 0 || target >= root.sequence.length)
            return;
        root.sequenceIndex = target;
        root.showImage(root.sequence[target].handle, root.sequence[target].name);
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
        root.sequence = [];
        root.sequenceIndex = -1;
    }

    Flickable {
        id: flick
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        // Not anchors.fill: the strip below takes the bottom of the window when there
        // is more than one image, and the fit maths reads flick's height.
        anchors.bottom: navBar.visible ? navBar.top : parent.bottom
        // Focused so the Esc and arrow handlers below are the ones the keys reach.
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
        Keys.onLeftPressed: root.step(-1)
        Keys.onRightPressed: root.step(1)

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

    // Only when there is somewhere to step to: a lone image keeps the whole window,
    // which is what the fit-in-both-directions item just made the point of.
    Rectangle {
        id: navBar

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        implicitHeight: Theme.rowHeight.toolbar
        // sequenceIndex too, not just the length: opened on a handle the snapshot does
        // not hold, the strip would otherwise stand with both buttons dead.
        visible: root.sequenceIndex >= 0 && root.sequence.length > 1
        // A ground of its own rather than the window's, matching the PDF and video
        // viewers' strips.
        color: "#2b2b2b"

        RowLayout {
            anchors.centerIn: parent
            spacing: Theme.spacing.md

            ToolButton {
                id: previous

                // Focus stays on the Flickable, which carries the arrow keys; Space
                // would otherwise re-trigger whichever button holds it.
                focusPolicy: Qt.NoFocus
                implicitWidth: 32
                implicitHeight: 32
                enabled: root.canGoPrevious
                text: Theme.glyph.chevronLeft
                font.family: Theme.font.iconFamily
                // Spelled out rather than left to the style: the button sits on the
                // dark strip, where the style's own text colour is the one picked for
                // a light surface.
                contentItem: Text {
                    text: previous.text
                    font: previous.font
                    color: previous.enabled ? "#ffffff" : "#808080"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: root.step(-1)
            }

            Label {
                text: root.sequenceIndex >= 0 ? (root.sequenceIndex + 1) + " / "
                                                + root.sequence.length : ""
                color: "#ffffff"
                font.pixelSize: Theme.font.caption
                horizontalAlignment: Text.AlignHCenter
                // Fixed so the strip does not jump as the position widens.
                Layout.minimumWidth: 72
            }

            ToolButton {
                id: next

                focusPolicy: Qt.NoFocus
                implicitWidth: 32
                implicitHeight: 32
                enabled: root.canGoNext
                text: Theme.glyph.chevronRight
                font.family: Theme.font.iconFamily
                contentItem: Text {
                    text: next.text
                    font: next.font
                    color: next.enabled ? "#ffffff" : "#808080"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                onClicked: root.step(1)
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
