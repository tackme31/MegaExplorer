import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts

// Explorer 11's status bar (extracted from Main.qml in R6-3): the item/selection
// count, the download and upload progress pairs, and the view-mode toggle.
// Loaded by Main.qml's footer: slot only while signed in.
ToolBar {
    id: root

    // The active tab's TabContentPane -- Main.qml's window.currentPane, which
    // its Binding keeps pointed at whichever pane is showing. Live-bound rather
    // than read once (unlike TabContentPane's own initial* injections): the
    // toggle below has to follow tab switches.
    //
    // Untyped on purpose. A TabContentPane-typed property would pull a views/
    // import into components/, reversing the direction those two depend on.
    required property var currentPane

    // Explorer 11's status bar, measured (S9). Left alone this row was 48px --
    // the height of the buttons in it plus Fluent's padding.
    implicitHeight: Theme.rowHeight.status

    // Same three-way statement of the band as the address-bar row, for the same
    // reason: a ToolBar is a Pane, so the RowLayout below is a child of the
    // implicit content item, and that item is sized to
    // contentWidth/contentHeight -- which default to what's inside rather than
    // to what's available (S8a's hand-off note to S9).
    topPadding: 0
    bottomPadding: 0
    leftPadding: Theme.spacing.md
    rightPadding: Theme.spacing.md
    contentWidth: availableWidth
    contentHeight: availableHeight

    // Chrome, like the caption row and the side panel -- not part of the
    // file-list surface (D3 leaves the footer's side unstated).
    background: Rectangle {
        color: Theme.color.surfaceAlt

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: Theme.border.thin
            color: Theme.color.stroke
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacing.sm

        // Explorer's own wording: the item count is always there, the selection
        // only while there is one. Reached through currentPane like the
        // view-mode buttons below, so the whole footer reads the active tab by
        // one route -- ?./?? for the brief login/logout window with no current
        // tab (see AddressToolBar.qml's Back button), and for the startup turn
        // in which this row exists before Main.qml's Binding has assigned one.
        Label {
            id: countLabel

            readonly property var listModel: root.currentPane?.navController?.fileListModel ?? null
            readonly property int selectedCount: countLabel.listModel?.selectedHandles.length ?? 0

            Layout.alignment: Qt.AlignVCenter
            color: Theme.color.textSecondary
            font.pixelSize: Theme.font.caption
            text: !countLabel.listModel ? "" : countLabel.selectedCount > 0 ? qsTr(
                                                                                  "%1 items  |  %2 selected").arg(
                                                                                  countLabel.listModel.count).arg(
                                                                                  countLabel.selectedCount) :
                                                                              qsTr("%1 items").arg(
                                                                                  countLabel.listModel.count)
        }

        // Every item in this row states its own vertical placement: none of them
        // can fill a 28px band, and a RowLayout's default for that case is not
        // something to lean on (S8a).
        Label {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            visible: downloadController.downloadActive
            elide: Text.ElideMiddle
            font.pixelSize: Theme.font.caption
            text: downloadController.activeFileName
        }
        ProgressBar {
            Layout.preferredWidth: 160
            Layout.alignment: Qt.AlignVCenter
            visible: downloadController.downloadActive
            from: 0
            to: 1
            value: downloadController.activeProgress
        }

        // Uploads run on their own serial queue alongside downloads, so both
        // pairs can be showing at once. "n remaining" is the only progress cue a
        // serial queue offers beyond the active file.
        Label {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            visible: uploadController.uploadActive
            elide: Text.ElideMiddle
            font.pixelSize: Theme.font.caption
            text: uploadController.pendingCount > 1 ? qsTr("↑ %1 (%2 remaining)").arg(
                                                          uploadController.activeFileName).arg(
                                                          uploadController.pendingCount - 1) : qsTr(
                                                          "↑ %1").arg(
                                                          uploadController.activeFileName)
        }
        ProgressBar {
            Layout.preferredWidth: 160
            Layout.alignment: Qt.AlignVCenter
            visible: uploadController.uploadActive
            from: 0
            to: 1
            value: uploadController.activeProgress
        }

        // Keeps the view-mode buttons right-aligned when the transfer groups
        // above are hidden (RowLayout excludes invisible items).
        Item {
            Layout.fillWidth: true
            visible: !downloadController.downloadActive && !uploadController.uploadActive
        }

        // Reads/writes the injected currentPane (the active tab's pane), not
        // Main.qml's window.viewMode -- each tab has its own independent view
        // mode since Phase 9; window.viewMode is only the last-write-wins
        // *persisted default*, updated via TabContentPane's viewModeWriteBack,
        // not the on-screen state of any particular tab.
        // checkable stays false on both, with `checked` a pure display binding
        // and onClicked the only thing that changes anything -- the same shape,
        // and the same reason, as TabStrip.qml's tab buttons. With checkable
        // true, AbstractButton writes `checked` itself on release; the binding
        // survives but its value is overridden until something it depends on
        // notifies. Clicking the mode already showing assigns the viewMode it
        // already has, so nothing notifies, and the button sits there
        // un-highlighted until the other one is clicked.
        StatusIconButton {
            text: Theme.glyph.viewList
            ToolTip.text: qsTr("Details")
            checkable: false
            checked: (root.currentPane?.viewMode ?? 0) === 0
            onClicked: if (root.currentPane)
                           root.currentPane.viewMode = 0
        }
        StatusIconButton {
            text: Theme.glyph.viewGrid
            ToolTip.text: qsTr("Extra large icons")
            checkable: false
            checked: (root.currentPane?.viewMode ?? 0) === 1
            onClicked: if (root.currentPane)
                           root.currentPane.viewMode = 1
        }
    }

    // The address bar's ToolbarIconButton (AddressToolBar.qml) has a
    // counterpart's worth of overlap with this, but they are declared
    // separately, not derived one from the other (which S7-e anticipated): the
    // two properties that matter here, size and checked appearance, are exactly
    // the ones that would have to change, so sharing a base would put the header
    // row -- measured and signed off in S7/S8b -- in the blast radius.
    //
    // Fluent draws `checked` as an accent slab, far too loud for a status bar
    // (S9). Explorer's own view-mode buttons use a subtle fill with an accent
    // underline, which is what the background below is.
    //
    // Nothing here may reference `root`: an inline component does not share the
    // scope of the file declaring it, so a root.* binding would be a
    // ReferenceError the moment this type is used from another file.
    component StatusIconButton: ToolButton {
        id: statusButton

        // Without this, clicking the button for the mode already showing changes
        // nothing, so no StackLayout focus handoff fires and focus stays here --
        // leaving arrow keys dead until the view is re-clicked.
        focusPolicy: Qt.NoFocus
        implicitWidth: 24
        implicitHeight: 24
        Layout.alignment: Qt.AlignVCenter
        font.family: Theme.font.iconFamily
        // Stated, unlike AddressToolBar.qml's ToolbarIconButton, because the
        // default body size in a 24px button leaves the glyph looking
        // incidental. Same 16 as the row-leading icons.
        font.pixelSize: Theme.iconSize.sm
        ToolTip.delay: 500
        ToolTip.visible: hovered

        // Fluent flips the label to highlighted-text while checked, which goes
        // with the accent slab it would otherwise be sitting on. Having taken
        // that slab away, the glyph has to be told to stay ink-coloured --
        // otherwise the checked button is white on near-white in light mode.
        contentItem: Text {
            text: statusButton.text
            font: statusButton.font
            color: Theme.color.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            radius: Theme.radius.sm
            color: statusButton.pressed ? Theme.color.subtlePressed : (statusButton.hovered
                                                                       || statusButton.checked)
                                          ? Theme.color.subtleHover : "transparent"

            Rectangle {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.spacing.sm
                anchors.rightMargin: Theme.spacing.sm
                height: Theme.border.drop
                radius: height / 2
                color: Theme.color.accent
                visible: statusButton.checked
            }
        }
    }
}
