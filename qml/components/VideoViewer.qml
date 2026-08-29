import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import MegaExplorer

// The in-app video viewer: a window of its own, playing a file's original bytes
// straight off the SDK's local HTTP server. That server answers Range requests,
// which is what lets the seek bar below jump without downloading the file first.
//
// One instance per opened video, created and destroyed by Main.qml exactly as
// ImageViewer is; the two stay separate files because only the window frame is
// common and every control below is Qt Multimedia's.
//
// controller is untyped `var` for ImageViewer.qml's reasons: a typed property
// would drag a views/ import into components/, and injecting the controller
// rather than reading the context property keeps the file loadable by the QML
// test harness, which installs no context properties.
Window {
    id: root

    required property var controller

    property var currentHandle: undefined
    property string currentName: ""
    property url source: ""

    readonly property bool showing: root.visible
    readonly property bool failed: root.showing && (String(root.source) === "" || player.error
                                                    !== MediaPlayer.NoError)

    width: 960
    height: 640
    minimumWidth: 320
    minimumHeight: 240
    title: root.currentName
    // Its own dark ground in both themes, the way video players generally are.
    color: "#1c1c1c"

    function open(handle, name) {
        if (!root.controller || root.controller.viewerKind(name) !== "video")
            return;
        root.currentHandle = handle;
        root.currentName = name;
        root.source = root.controller.sourceUrl(handle);
        root.show();
        root.raise();
        root.requestActivate();
    }

    // Hiding is the only teardown path -- the native close button, close() and a
    // plain visible = false all land here, so playback stops once for all three.
    onVisibleChanged: {
        if (!root.visible)
            root.releaseVideo();
    }

    function releaseVideo() {
        // Stopped before the source goes: the backend keeps pulling from the local
        // HTTP server for as long as it is playing, and destroying the window is
        // not by itself a request to stop.
        player.stop();
        root.currentHandle = undefined;
        root.currentName = "";
        root.source = "";
    }

    function formatTime(milliseconds) {
        const total = Math.max(0, Math.floor(milliseconds / 1000));
        const seconds = String(total % 60).padStart(2, "0");
        const minutes = Math.floor(total / 60) % 60;
        const hours = Math.floor(total / 3600);
        if (hours > 0)
            return hours + ":" + String(minutes).padStart(2, "0") + ":" + seconds;
        return minutes + ":" + seconds;
    }

    MediaPlayer {
        id: player

        source: root.source
        videoOutput: output
        // Playback starts once the media has loaded, which saves open() from having
        // to guess when the source is ready to accept play().
        autoPlay: true
        audioOutput: AudioOutput {}
    }

    // The slider follows playback except while the user has hold of it. A plain
    // binding would not do: dragging the handle assigns to value, which breaks the
    // binding for good.
    Binding {
        target: seek
        property: "value"
        value: player.position
        when: !seek.pressed
        restoreMode: Binding.RestoreNone
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        // Focused so the Esc handler below is the one the keys reach, off the item
        // rather than a window-scoped Shortcut: QWindow::isActive() is true for a
        // transient child whenever its parent is active, so with two viewers up both
        // Shortcuts matched and QShortcutMap dropped the key as ambiguous.
        focus: true
        Keys.onEscapePressed: root.close()

        VideoOutput {
            id: output

            Layout.fillWidth: true
            Layout.fillHeight: true
            fillMode: VideoOutput.PreserveAspectFit

            BusyIndicator {
                anchors.centerIn: parent
                running: root.showing && String(root.source) !== "" && (player.mediaStatus
                                                                        === MediaPlayer.LoadingMedia
                                                                        || player.mediaStatus
                                                                        === MediaPlayer.StalledMedia)
                visible: running
            }

            Label {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.spacing.xl
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: "#ffffff"
                // The two failures kept apart: no URL means the local server would not
                // start, a player error means the stream did not decode.
                text: String(root.source) === "" ? "This file could not be opened." :
                                                   "This video could not be played."
                visible: root.failed
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.rowHeight.toolbar
            // A ground of its own rather than the window's: the transport controls
            // have to stay legible over whatever frame the video is showing.
            color: "#2b2b2b"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing.md
                anchors.rightMargin: Theme.spacing.md
                spacing: Theme.spacing.md

                ToolButton {
                    id: playPause

                    // Focus stays on the layout above, which is what carries the Esc
                    // handler; Space would otherwise re-trigger this button.
                    focusPolicy: Qt.NoFocus
                    implicitWidth: 32
                    implicitHeight: 32
                    Layout.alignment: Qt.AlignVCenter
                    enabled: String(root.source) !== "" && !root.failed
                    text: player.playbackState === MediaPlayer.PlayingState ? Theme.glyph.pause :
                                                                              Theme.glyph.play
                    font.family: Theme.font.iconFamily
                    // Spelled out rather than left to the style: the button sits on the
                    // dark strip above, where the style's own text colour is the one
                    // picked for a light surface.
                    contentItem: Text {
                        text: playPause.text
                        font: playPause.font
                        color: playPause.enabled ? "#ffffff" : "#808080"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() :
                                                                                   player.play()
                }

                Label {
                    text: root.formatTime(player.position)
                    color: "#ffffff"
                    font.pixelSize: Theme.font.caption
                    Layout.alignment: Qt.AlignVCenter
                }

                Slider {
                    id: seek

                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    focusPolicy: Qt.NoFocus
                    from: 0
                    // Never zero: a Slider whose range is empty puts its handle at the
                    // far end and reports every press as the maximum.
                    to: Math.max(1, player.duration)
                    enabled: player.seekable
                    onMoved: player.position = seek.value
                }

                Label {
                    text: root.formatTime(player.duration)
                    color: "#ffffff"
                    font.pixelSize: Theme.font.caption
                    Layout.alignment: Qt.AlignVCenter
                }
            }
        }
    }
}
