import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts

// The minimized transfer flyout: one bar in the window's bottom-right corner
// showing how far the current run of transfers has got, downloads and uploads
// counted together. It replaces the per-file progress pair the status bar used
// to carry.
//
// This item owns the bottom-right corner; ToastStack.qml yields upward by the
// reservedHeight below. Keeping one owner of those coordinates is the whole
// lesson of S10, recorded at the top of that file -- three Popups each pinning
// themselves to the same corner is how they ended up stacked on each other.
//
// Expects to be a child of ApplicationWindow's contentItem, for the same reason
// ToastStack does: the window Overlay extends under the status bar.
Rectangle {
    id: root

    // main.cpp's transferListModel, injected rather than named here so a stub
    // can drive the component in tests. Untyped because a context property has
    // no QML-visible type.
    required property var transfers

    readonly property bool runActive: root.transfers?.runActive ?? false
    readonly property int total: root.transfers?.runTotal ?? 0
    readonly property int finished: root.transfers?.runFinished ?? 0

    // What ToastStack.qml adds under its own bottom margin. Zero while hidden, so
    // the toasts drop back to the corner when this goes away. Keyed off `shown`
    // rather than `visible`: an Item's visible is *effective* visibility, so it
    // reads false whenever an ancestor is hidden -- which would silently give the
    // corner away.
    readonly property int reservedHeight: root.shown ? root.height + Theme.toast.margin : 0

    property bool shown: false

    // Set when the flyout opened itself because a run started. A hand-opened one
    // (the More menu) is not auto-hidden -- otherwise choosing "Transfers" with
    // nothing in flight would be a three-second flash.
    property bool autoOpened: false

    // Split out of the Label below so the wording can be tested without reaching
    // into the layout, the same split ToastStack.qml's describe* functions are.
    function summaryText(): string {
        if (root.total <= 0)
            return qsTr("No transfers");
        return qsTr("%1 / %2").arg(root.finished).arg(root.total);
    }

    // The More menu's entry point.
    function show(): void {
        root.shown = true;
        root.autoOpened = false;
    }

    function hide(): void {
        root.shown = false;
        root.autoOpened = false;
    }

    onRunActiveChanged: {
        if (!root.runActive)
            return;
        root.shown = true;
        root.autoOpened = true;
    }

    // Same corner and same margin as the toast stack, bound rather than anchored
    // the way that file and DragProxy do it.
    x: parent ? parent.width - width - Theme.toast.margin : 0
    y: parent ? parent.height - height - Theme.toast.margin : 0
    width: Math.min(Theme.toast.maxWidth, (parent ? parent.width : Theme.toast.maxWidth)
                    - Theme.toast.margin * 2)
    implicitHeight: layout.implicitHeight + Theme.spacing.lg * 2
    height: implicitHeight

    opacity: root.shown ? 1 : 0
    // `shown` on its own, not just a non-zero opacity: the fade's first tick can
    // be a frame or more away (and never arrives at all if the animation driver
    // is stalled), which would leave the bar invisible after it was asked for.
    // The opacity half keeps it drawn while it fades back out.
    visible: root.shown || opacity > 0

    // The chrome surface, not `surface`: the file list behind this bar is
    // `surface`, so painting it the same colour would leave a 1px stroke as the
    // only thing separating a floating bar from the content under it (D3).
    color: Theme.color.surfaceAlt
    border.width: Theme.border.thin
    border.color: Theme.color.stroke
    radius: Theme.radius.md

    Behavior on opacity {
        NumberAnimation {
            duration: Theme.motion.fast
        }
    }

    // Only ever hides the bar. Cancelling is not on this surface: the run keeps
    // going, which is what the roadmap asks for and what Explorer's own transfer
    // popup does.
    Timer {
        interval: Theme.transfer.idleHideMs
        running: root.shown && root.autoOpened && !root.runActive
        onTriggered: root.hide()
    }

    // Swallows clicks that land on the bar -- without it this is a hole you can
    // band-select files through. Declared before the row below so the close
    // button still takes its own clicks.
    MouseArea {
        anchors.fill: parent
    }

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.spacing.lg
        spacing: Theme.spacing.md

        ProgressBar {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            visible: root.total > 0
            from: 0
            to: 1
            value: root.transfers?.runProgress ?? 0
        }

        Label {
            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: root.total === 0
            color: Theme.color.textSecondary
            font.pixelSize: Theme.font.caption
            text: root.summaryText()
        }

        ToolButton {
            id: closeButton

            Layout.alignment: Qt.AlignVCenter
            // Same squeeze as ToastStack's close button: Fluent's icon-only
            // ToolButton is 38x32 and the padding alone cannot get under the
            // background's implicit 32, so the background is replaced too. All
            // four paddings are spelled out because Fluent binds each side
            // individually and beats the grouped shorthand.
            topPadding: Theme.spacing.xs
            bottomPadding: Theme.spacing.xs
            leftPadding: Theme.spacing.xs
            rightPadding: Theme.spacing.xs
            implicitWidth: 20
            implicitHeight: 20
            background: Rectangle {
                radius: Theme.radius.sm
                color: closeButton.pressed ? Theme.color.subtlePressed : closeButton.hovered
                                             ? Theme.color.subtleHover : "transparent"
            }
            font.family: Theme.font.iconFamily
            font.pixelSize: 10
            text: Theme.glyph.close
            ToolTip.text: qsTr("Hide")
            ToolTip.delay: 500
            ToolTip.visible: hovered
            focusPolicy: Qt.NoFocus
            onClicked: root.hide()
        }
    }
}
