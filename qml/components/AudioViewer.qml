import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts
import QtMultimedia
import MegaExplorer

// The in-app audio viewer: the same window-per-file shape as VideoViewer, playing a
// file's original bytes off the SDK's local HTTP server so the seek bar can jump
// without downloading first.
//
// A file of its own rather than a mode of VideoViewer: there is no VideoOutput to
// hand the player, and the space that frame would fill is what carries the file's
// identity instead. It keeps the app's own surface colours where VideoViewer commits
// to a dark ground -- nothing here has to stay legible over a video frame.
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
    readonly property bool failed: root.showing && (String(root.source) === "" || player.error
                                                    !== MediaPlayer.NoError)
    readonly property bool loading: root.showing && String(root.source) !== "" && (
                                        player.mediaStatus === MediaPlayer.LoadingMedia
                                        || player.mediaStatus === MediaPlayer.StalledMedia)

    // Audio settings on the slider's own 0..1 scale. Main.qml owns the value (it is
    // app-wide and persisted), so this window asks for a change rather than writing
    // one: assigning here would break the binding that keeps sibling viewers in step.
    property real volume: 1.0
    property bool muted: false

    signal volumeRequested(real level)
    signal muteToggleRequested

    width: 520
    height: 280
    minimumWidth: 360
    minimumHeight: 220
    title: root.currentName
    color: Theme.color.surface

    function open(handle, name) {
        if (!root.controller || root.controller.viewerKind(name) !== "audio")
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
            root.releaseAudio();
    }

    function releaseAudio() {
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
        // No videoOutput: an audio file has no video track, and a MediaPlayer with
        // none attached still decodes and plays the audio one.
        //
        // Playback starts once the media has loaded, which saves open() from having
        // to guess when the source is ready to accept play().
        autoPlay: true
        audioOutput: AudioOutput {
            // Cubed, not passed straight through: this property is a linear
            // amplitude, and Qt's own docs say a UI control wants a nonlinear scale
            // because perceived loudness is roughly logarithmic. The conversion Qt
            // offers for it, QtAudio::convertVolume(), is C++-only.
            volume: root.volume * root.volume * root.volume
            muted: root.muted
        }
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

    // Same reason as the seek slider above: dragging assigns to value, so the level
    // coming back down from Main.qml has to be re-applied rather than bound.
    Binding {
        target: volumeSlider
        property: "value"
        value: root.volume
        when: !volumeSlider.pressed
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

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.spacing.xl
                spacing: Theme.spacing.md

                FileIcon {
                    Layout.alignment: Qt.AlignHCenter
                    isFolder: false
                    fileName: root.currentName
                    size: Theme.iconSize.lg
                }

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    // Names run long and the window is narrow; wrapping instead would
                    // push the transport bar's row height around as the title grows.
                    elide: Text.ElideMiddle
                    text: root.currentName
                    color: Theme.color.text
                }

                BusyIndicator {
                    Layout.alignment: Qt.AlignHCenter
                    running: root.loading
                    visible: running
                }

                Label {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: Theme.color.textSecondary
                    // The two failures kept apart: no URL means the local server would
                    // not start, a player error means the stream did not decode.
                    text: String(root.source) === "" ? "This file could not be opened." :
                                                       "This audio could not be played."
                    visible: root.failed
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.rowHeight.toolbar
            color: Theme.color.surfaceAlt

            Rectangle {
                anchors.top: parent.top
                width: parent.width
                height: Theme.border.thin
                color: Theme.color.stroke
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing.md
                anchors.rightMargin: Theme.spacing.md
                spacing: Theme.spacing.md

                ToolButton {
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
                    onClicked: player.playbackState === MediaPlayer.PlayingState ? player.pause() :
                                                                                   player.play()
                }

                Label {
                    text: root.formatTime(player.position)
                    color: Theme.color.textSecondary
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
                    color: Theme.color.textSecondary
                    font.pixelSize: Theme.font.caption
                    Layout.alignment: Qt.AlignVCenter
                }

                ToolButton {
                    objectName: "muteButton"
                    focusPolicy: Qt.NoFocus
                    implicitWidth: 32
                    implicitHeight: 32
                    Layout.alignment: Qt.AlignVCenter
                    text: root.muted ? Theme.glyph.mute : Theme.glyph.volume
                    font.family: Theme.font.iconFamily
                    onClicked: root.muteToggleRequested()
                }

                Slider {
                    id: volumeSlider

                    objectName: "volumeSlider"
                    Layout.preferredWidth: 96
                    Layout.alignment: Qt.AlignVCenter
                    focusPolicy: Qt.NoFocus
                    from: 0
                    to: 1
                    enabled: !root.muted
                    onMoved: root.volumeRequested(volumeSlider.value)
                }
            }
        }
    }
}
