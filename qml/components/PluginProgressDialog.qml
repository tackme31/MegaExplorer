import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts

// One instance in Main.qml: a row per command running (a "progress": true one at
// once, any other once it has run a few seconds), and per
// finished "result": "dialog" command until its Close. A row that was showing
// progress turns into the result in place.
// Modal: changing items behind a run (tags, logout) can undo or misdirect its work.
// A long run is left with Cancel, or another window of the app.
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

    function formatCount(n) {
        return Number(n).toLocaleString(Qt.locale(), "f", 0);
    }

    // The line under the bar: the figures the command's plugin.json "progress.show"
    // asks for, always in this order, e.g. "1,234/2,000 (61%) · 12/s · 10:21 elapsed, ~0:05 left".
    function statsText(run, now) {
        const show = run.show ?? [];
        const running = !run.finished;
        const parts = [];
        if (show.includes("count") && !run.preparing && run.current >= 0) {
            if (run.total > 0) {
                const percent = Math.floor(Math.min(run.current, run.total) * 100 / run.total);
                parts.push(qsTr("%1/%2 (%3%)").arg(root.formatCount(run.current))
                           .arg(root.formatCount(run.total)).arg(percent));
            } else {
                parts.push(root.formatCount(run.current));
            }
        }
        if (show.includes("rate") && running && run.rate >= 0)
            parts.push(qsTr("%1/s").arg(root.formatCount(Math.floor(run.rate))));
        const times = [];
        if (show.includes("elapsed"))
            times.push(qsTr("%1 elapsed").arg(root.formatElapsed((run.finished ? run.finishedAt : now) - run.startedAt)));
        if (show.includes("remaining") && running && run.rate > 0 && run.total > 0 && run.current < run.total) {
            // Counted down from the last report, so a slow-reporting run does not look frozen.
            const left = (run.total - run.current) / run.rate * 1000 - (now - run.rateAt);
            times.push(qsTr("~%1 left").arg(root.formatElapsed(left)));
        }
        if (times.length > 0)
            parts.push(times.join(qsTr(", ")));
        return parts.join(" · ");
    }

    // outcome as PluginRun::finished; the same cases as ToastStack.showPluginResult.
    function statusText(run) {
        switch (run.outcome) {
        case "ok":
            return qsTr("Done");
        case "error":
            return qsTr("Failed");
        case "notFound":
            return qsTr("Can't find \"%1\"").arg(run.result);
        case "failedToStart":
            return qsTr("Couldn't be started");
        case "timeout":
            return qsTr("Didn't start in time and was stopped");
        default:
            return qsTr("Stopped unexpectedly");
        }
    }

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    closePolicy: Popup.NoAutoClose
    visible: root.runs.length > 0
    width: Math.min(520, Overlay.overlay.width - 48)
    title: root.runs.length === 1 ? root.runs[0].pluginName : qsTr("Plugins")

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
                readonly property bool finished: row.modelData.finished
                readonly property bool counted: !row.finished && !row.modelData.preparing
                                                && row.modelData.total > 0
                // notFound's message is the program it looked for, already in the status line.
                readonly property bool hasResult: row.finished && row.modelData.result !== ""
                                                  && (row.modelData.outcome === "ok"
                                                      || row.modelData.outcome === "error")

                Layout.fillWidth: true
                spacing: Theme.spacing.sm

                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: root.runs.length === 1 ? row.modelData.commandTitle : qsTr("%1: %2").arg(
                                                       row.modelData.pluginName).arg(
                                                       row.modelData.commandTitle)
                }

                Label {
                    Layout.fillWidth: true
                    elide: Text.ElideMiddle
                    color: row.finished && row.modelData.outcome !== "ok" ? Theme.color.danger :
                                                                            Theme.color.textSecondary
                    font.pixelSize: Theme.font.caption
                    text: row.finished ? root.statusText(row.modelData)
                          : row.modelData.cancelling ? qsTr("Cancelling…")
                          : row.modelData.preparing ? qsTr("Preparing…")
                          : row.modelData.message
                }

                ProgressBar {
                    visible: !row.finished
                    Layout.fillWidth: true
                    indeterminate: !row.counted
                    from: 0
                    to: row.counted ? row.modelData.total : 1
                    value: row.counted ? Math.min(row.modelData.current, row.modelData.total) : 0
                }

                Label {
                    Layout.fillWidth: true
                    visible: text !== ""
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideLeft
                    color: Theme.color.textSecondary
                    font.pixelSize: Theme.font.caption
                    text: root.statsText(row.modelData, root.now)
                }

                ScrollView {
                    id: resultView

                    visible: row.hasResult
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(resultText.implicitHeight, 240)

                    TextArea {
                        id: resultText

                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        textFormat: TextEdit.PlainText
                        text: row.hasResult ? row.modelData.result : ""
                    }
                }

                RowLayout {
                    visible: row.finished
                    Layout.alignment: Qt.AlignRight
                    spacing: Theme.spacing.md

                    Button {
                        visible: row.hasResult
                        text: qsTr("Copy")
                        onClicked: {
                            resultText.selectAll();
                            resultText.copy();
                            resultText.deselect();
                        }
                    }

                    Button {
                        text: qsTr("Close")
                        onClicked: root.plugins.dismissResult(row.modelData.runId)
                    }
                }

                Button {
                    visible: !row.finished
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
