import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts

// One instance in Main.qml, open while any "progress": true plugin command runs.
// Not modal: a run over thousands of items must not keep the user out of the window.
Dialog {
    id: root

    required property var plugins

    readonly property var runs: root.plugins.progressRuns
    property double now: Date.now()

    function formatElapsed(ms) {
        const seconds = Math.max(0, Math.floor(ms / 1000));
        const h = Math.floor(seconds / 3600);
        const m = Math.floor(seconds / 60) % 60;
        const s = String(seconds % 60).padStart(2, "0");
        return h > 0 ? `${h}:${String(m).padStart(2, "0")}:${s}` : `${m}:${s}`;
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: false
    closePolicy: Popup.NoAutoClose
    visible: root.runs.length > 0
    width: Math.min(420, Overlay.overlay.width - 48)
    title: root.runs.length === 1 ? root.runs[0].pluginName : qsTr("Plugins running")

    onVisibleChanged: root.now = Date.now()

    Timer {
        interval: 1000
        repeat: true
        running: root.visible
        onTriggered: root.now = Date.now()
    }

    ColumnLayout {
        width: root.availableWidth
        spacing: Theme.spacing.lg

        Repeater {
            model: root.runs

            delegate: ColumnLayout {
                id: row

                required property var modelData
                readonly property bool counted: !row.modelData.preparing && row.modelData.total > 0

                Layout.fillWidth: true
                spacing: Theme.spacing.sm

                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: root.runs.length === 1 ? row.modelData.commandTitle : qsTr("%1: %2").arg(
                                                       row.modelData.pluginName).arg(
                                                       row.modelData.commandTitle)
                }

                ProgressBar {
                    Layout.fillWidth: true
                    indeterminate: !row.counted
                    from: 0
                    to: row.counted ? row.modelData.total : 1
                    value: row.counted ? Math.min(row.modelData.current, row.modelData.total) : 0
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing.md

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        color: Theme.color.textSecondary
                        font.pixelSize: Theme.font.caption
                        text: row.modelData.cancelling ? qsTr("Cancelling…") : row.modelData.preparing ? qsTr(
                                                                                                    "Preparing…") :
                                                                                                row.modelData.message
                    }

                    Label {
                        visible: row.counted
                        color: Theme.color.textSecondary
                        font.pixelSize: Theme.font.caption
                        text: qsTr("%1 / %2").arg(Math.max(0, row.modelData.current)).arg(
                                  row.modelData.total)
                    }

                    Label {
                        color: Theme.color.textSecondary
                        font.pixelSize: Theme.font.caption
                        text: qsTr("Elapsed %1").arg(root.formatElapsed(root.now
                                                                        - row.modelData.startedAt))
                    }
                }

                Button {
                    Layout.alignment: Qt.AlignRight
                    enabled: !row.modelData.cancelling || row.modelData.forceStoppable
                    text: row.modelData.forceStoppable ? qsTr("Force quit") : qsTr("Cancel")
                    onClicked: row.modelData.forceStoppable ? root.plugins.forceStop(row.modelData.pluginId) :
                                                              root.plugins.cancel(row.modelData.pluginId)
                }
            }
        }
    }
}
