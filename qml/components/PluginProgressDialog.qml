import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts

// One instance in Main.qml: a row per run whose plugin has the progress dialog
// open (ui.showProgress, or "progressDialog": true at start). What a row says is
// entirely the plugin's: title, message, a bar at value, and detail.
// Modal: changing items behind a run (tags, logout) can undo or misdirect its work.
// A long run is left with Cancel, or another window of the app.
Dialog {
    id: root

    required property var plugins

    readonly property var runs: root.plugins.progressRuns
    // What is drawn: the last non-empty runs, so the close fade keeps its rows and
    // title. That fade can sit on screen while a ui.reveal's folder loads.
    property var shownRuns: []

    function keepRuns() {
        if (root.runs.length > 0)
            root.shownRuns = root.runs;
    }
    onRunsChanged: root.keepRuns()
    Component.onCompleted: root.keepRuns()

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    closePolicy: Popup.NoAutoClose
    visible: root.runs.length > 0
    width: Math.min(520, Overlay.overlay.width - 48)
    title: root.shownRuns.length === 1 ? root.shownRuns[0].pluginName : qsTr("Plugins")

    ColumnLayout {
        width: root.availableWidth
        spacing: Theme.spacing.lg

        Repeater {
            model: root.shownRuns

            delegate: ColumnLayout {
                id: row

                required property var modelData
                readonly property bool counted: row.modelData.value >= 0

                Layout.fillWidth: true
                spacing: Theme.spacing.sm

                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: root.shownRuns.length === 1 ? row.modelData.title : qsTr("%1: %2").arg(
                                                            row.modelData.pluginName).arg(row.modelData.title)
                }

                Label {
                    Layout.fillWidth: true
                    visible: text !== ""
                    elide: Text.ElideMiddle
                    color: Theme.color.textSecondary
                    font.pixelSize: Theme.font.caption
                    text: row.modelData.cancelling ? qsTr("Cancelling…") : row.modelData.message
                }

                ProgressBar {
                    Layout.fillWidth: true
                    indeterminate: !row.counted
                    from: 0
                    to: 1
                    value: row.counted ? row.modelData.value : 0
                }

                Label {
                    Layout.fillWidth: true
                    visible: text !== ""
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideLeft
                    color: Theme.color.textSecondary
                    font.pixelSize: Theme.font.caption
                    text: row.modelData.detail
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
