import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts

// The transfer flyout in the window's bottom-right corner. Minimised it is one
// bar saying how far the current run has got, downloads and uploads counted
// together; expanded it adds a row per transfer of the session and the button
// that stops both queues. It replaces the per-file progress pair the status bar
// used to carry.
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

    // Minimised (the summary bar alone) or expanded (that bar plus one row per
    // transfer). The rows include the ones that already finished: keeping them for
    // the session is what TransferListModel does it for.
    property bool expanded: false

    readonly property int rowCount: root.transfers?.count ?? 0

    // Ceiling on the row list in a window tall enough to grant it. A short one is
    // handled by clamping the whole panel instead -- see `height` below.
    readonly property int listMaxHeight: Theme.transfer.listMaxHeight

    // Emitted rather than calling the two controllers from here, so the component
    // still stands up in a test given nothing but a model stub.
    signal cancelAllRequested

    // Split out of the Label below so the wording can be tested without reaching
    // into the layout, the same split ToastStack.qml's describe* functions are.
    function summaryText(): string {
        if (root.total <= 0)
            return qsTr("No transfers");
        return qsTr("%1 / %2").arg(root.finished).arg(root.total);
    }

    // Active rows say how far they have got; the rest say what became of them.
    // Split out for the same reason summaryText() is.
    function stateText(state: int, progress: real): string {
        switch (state) {
        case TransferState.Queued:
            return qsTr("Queued");
        case TransferState.Active:
            return qsTr("%1%").arg(Math.round(progress * 100));
        case TransferState.Completed:
            return qsTr("Done");
        case TransferState.Failed:
            return qsTr("Failed");
        case TransferState.Cancelled:
            return qsTr("Cancelled");
        }
        return "";
    }

    // The More menu's entry point.
    function show(): void {
        root.shown = true;
        root.autoOpened = false;
    }

    function hide(): void {
        root.shown = false;
        root.autoOpened = false;
        // Collapsed on the way out, so a later run auto-opens as the small bar
        // rather than putting a full panel up unbidden.
        root.expanded = false;
    }

    // Expanding is a deliberate interaction, so it takes the bar off the idle
    // auto-hide for the same reason opening it from the More menu does.
    function toggleExpanded(): void {
        root.expanded = !root.expanded;
        if (root.expanded)
            root.autoOpened = false;
    }

    onRunActiveChanged: {
        if (!root.runActive || root.shown)
            return;
        // Only a bar that was down counts as auto-opened. Re-arming one that is
        // already up because it was asked for would take it away three seconds
        // after the run settles -- with the finished rows the panel exists to show.
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
    // Never taller than the window it floats in. Capping the row list alone would
    // not do it: the chrome around the list (margins, header, divider, button row)
    // is not in that budget, so a short window would still drive `y` negative and
    // push the panel off the top edge. Everything but the list carries a
    // Layout.minimumHeight, so the list is what gives way and scrolls.
    height: parent ? Math.min(implicitHeight, parent.height - Theme.toast.margin * 2) : implicitHeight

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

    // Only ever hides the bar; the run carries on behind it. Stopping a run is the
    // expanded panel's "Cancel all", which is a button you have to reach for.
    Timer {
        interval: Theme.transfer.idleHideMs
        running: root.shown && root.autoOpened && !root.runActive
        onTriggered: root.hide()
    }

    // Swallows clicks that land on the bar -- without it this is a hole you can
    // band-select files through. Declared before the layout below so the buttons
    // and the row list still take their own clicks.
    MouseArea {
        anchors.fill: parent
    }

    // Both header buttons wear the same squeeze as ToastStack's close button:
    // Fluent's icon-only ToolButton is 38x32 and the padding alone cannot get
    // under the background's implicit 32, so the background is replaced too. All
    // four paddings are spelled out because Fluent binds each side individually
    // and beats the grouped shorthand.
    component FlyoutIconButton: ToolButton {
        id: iconButton

        topPadding: Theme.spacing.xs
        bottomPadding: Theme.spacing.xs
        leftPadding: Theme.spacing.xs
        rightPadding: Theme.spacing.xs
        implicitWidth: 20
        implicitHeight: 20
        background: Rectangle {
            radius: Theme.radius.sm
            color: iconButton.pressed ? Theme.color.subtlePressed : iconButton.hovered
                                        ? Theme.color.subtleHover : "transparent"
        }
        font.family: Theme.font.iconFamily
        font.pixelSize: 10
        ToolTip.delay: 500
        ToolTip.visible: iconButton.hovered
        focusPolicy: Qt.NoFocus
    }

    ColumnLayout {
        id: layout
        anchors.fill: parent
        anchors.margins: Theme.spacing.lg
        spacing: Theme.spacing.md

        RowLayout {
            id: header

            Layout.fillWidth: true
            Layout.minimumHeight: header.implicitHeight
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

            FlyoutIconButton {
                Layout.alignment: Qt.AlignVCenter
                // The panel grows upward out of the corner, so up opens it.
                text: root.expanded ? Theme.glyph.chevronDown : Theme.glyph.chevronUp
                ToolTip.text: root.expanded ? qsTr("Collapse") : qsTr("Expand")
                onClicked: root.toggleExpanded()
            }

            FlyoutIconButton {
                Layout.alignment: Qt.AlignVCenter
                text: Theme.glyph.close
                ToolTip.text: qsTr("Hide")
                onClicked: root.hide()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            visible: root.expanded
            implicitHeight: Theme.border.thin
            color: Theme.color.stroke
        }

        ListView {
            id: rowList

            objectName: "transferRowList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(rowList.contentHeight, root.listMaxHeight)
            // The one item in the column allowed to shrink, which is what makes the
            // panel's own height clamp land here rather than on the header.
            Layout.fillHeight: true
            Layout.minimumHeight: 0
            visible: root.expanded && root.rowCount > 0
            clip: true
            spacing: Theme.spacing.sm
            model: root.transfers
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Item {
                id: transferRow

                width: rowList.width
                implicitHeight: rowLayout.implicitHeight
                height: implicitHeight

                RowLayout {
                    id: rowLayout

                    anchors.fill: parent
                    spacing: Theme.spacing.md

                    Label {
                        Layout.alignment: Qt.AlignVCenter
                        color: Theme.color.textSecondary
                        font.family: Theme.font.iconFamily
                        font.pixelSize: 12
                        // `model.` throughout this delegate rather than required
                        // properties: the state role would collide with Item's own
                        // `state`, and a half-qualified delegate reads worse than a
                        // fully qualified one.
                        text: model.direction === TransferDirection.Upload ? Theme.glyph.transferUp : Theme.glyph.transferDown
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing.xs

                        Label {
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                            font.pixelSize: Theme.font.caption
                            text: model.name
                        }

                        ProgressBar {
                            Layout.fillWidth: true
                            // Only the row actually moving gets a bar: a queued
                            // row's size is a guess until the SDK confirms it, and
                            // a settled one has its outcome in the label instead.
                            visible: model.state === TransferState.Active
                            from: 0
                            to: 1
                            value: model.progress
                        }
                    }

                    Label {
                        Layout.alignment: Qt.AlignVCenter
                        color: model.state === TransferState.Failed ? Theme.color.danger : Theme.color.textSecondary
                        font.pixelSize: Theme.font.caption
                        text: root.stateText(model.state, model.progress)
                    }
                }
            }
        }

        Label {
            id: emptyNotice

            Layout.fillWidth: true
            Layout.minimumHeight: emptyNotice.implicitHeight
            Layout.topMargin: Theme.spacing.sm
            Layout.bottomMargin: Theme.spacing.sm
            visible: root.expanded && root.rowCount === 0
            horizontalAlignment: Text.AlignHCenter
            color: Theme.color.textSecondary
            font.pixelSize: Theme.font.caption
            text: qsTr("Nothing has been transferred yet")
        }

        RowLayout {
            id: footer

            Layout.fillWidth: true
            Layout.minimumHeight: footer.implicitHeight
            visible: root.expanded
            spacing: Theme.spacing.md

            Item {
                Layout.fillWidth: true
            }

            Button {
                // Stops both queues. Nothing in flight means nothing to stop --
                // the finished rows above stay listed either way.
                objectName: "cancelAllButton"
                text: qsTr("Cancel all")
                enabled: root.runActive
                onClicked: root.cancelAllRequested()
            }
        }
    }
}
