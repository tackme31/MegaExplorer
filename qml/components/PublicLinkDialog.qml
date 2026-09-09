import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts

// The one place a node's public link is looked at rather than acted on: the URL
// itself, copyable, plus the way to revoke it. One instance per file view like
// ConfirmRemoveLinkDialog, and reached from the same context menu.
//
// Opening it on a node that was never shared *creates* the link (mutController
// .requestLink exports on demand), which is why there is no "create" button --
// the dialog has nothing to show until the export lands, so it does it itself.
// docs/investigations/STUDY_PUBLIC_LINK_SETTINGS.md section 2 has the shape;
// the expiry and password rows it describes are separate ROADMAP items.
Dialog {
    id: root

    required property var navController
    required property var mutController

    // Sampled by showForSelection(), not bound, exactly as in
    // ConfirmRemoveLinkDialog: a background refresh can prune the selection
    // while the dialog is open, and what is on screen has to stay what was
    // asked for.
    property string entryName: ""
    property var handle: 0

    // Empty once loading is over means the export failed or handed back no URL;
    // the field says so, because the toast carrying the reason is gone by the
    // time the user looks back at the dialog.
    property string link: ""
    property bool loading: false

    // The action is offered on a single node only (MenuActionResolver's
    // SingleOnly), so only the first entry is ever the target.
    function showForSelection() {
        const entries = root.navController.fileListModel.selectedEntries();
        if (entries.length === 0)
            return;
        root.entryName = entries[0].name;
        root.handle = entries[0].handle;
        root.link = "";
        root.loading = true;
        root.open();
        root.mutController.requestLink(root.handle);
    }

    signal removeLinkRequested

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Link settings")

    // A Popup takes its content's *implicit* width, and both the file name and
    // the URL are single unwrapped lines -- so without a cap either one drags
    // the frame past the window edge.
    readonly property real maxWidth: Overlay.overlay.width - 48
    width: Math.min(480, root.maxWidth)

    // The content column has to be given this width explicitly: a single item in
    // contentData becomes the contentItem, which a Popup does *not* stretch, so
    // every Layout.fillWidth under it would otherwise be a no-op and the frame
    // would show a dead gutter beside a clipped URL. Derived from the same
    // overlay figure `width` is, never from root.width or root.availableWidth --
    // reading those here closes a loop through the dialog's implicitHeight.
    // Not named contentWidth: Popup declares that FINAL, and the override costs
    // the whole component at load time, not just the property.
    readonly property real bodyWidth: Math.min(480, root.maxWidth) - root.leftPadding
                                      - root.rightPadding

    function linkFieldText(): string {
        if (root.loading)
            return qsTr("Creating link…");
        if (root.link === "")
            return qsTr("Unavailable");
        return root.link;
    }

    ColumnLayout {
        width: root.bodyWidth
        spacing: Theme.spacing.md

        Label {
            Layout.fillWidth: true
            elide: Text.ElideMiddle
            text: root.entryName
        }

        TextField {
            Layout.fillWidth: true
            readOnly: true
            text: root.linkFieldText()
            // A URL wider than the field scrolls to the cursor, which lands at
            // the end -- so without this the user is shown the tail of the link
            // and has to scroll back to recognise it.
            onTextChanged: cursorPosition = 0
            // Greyed while there is no URL to select, so the placeholder text
            // above cannot be mistaken for the link itself.
            color: root.link === "" ? Theme.color.textSecondary : Theme.color.text
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.font.caption
            color: Theme.color.textSecondary
            text: qsTr("Anyone with this link can open the item without signing in.")
        }
    }

    // A plain Item rather than a DialogButtonBox: the destructive button belongs
    // at the far left and the box lays its children out by role, which the
    // FluentWinUI3 style is free to order as it likes.
    footer: Item {
        implicitHeight: footerRow.implicitHeight + Theme.spacing.lg * 2

        RowLayout {
            id: footerRow
            anchors.fill: parent
            anchors.margins: Theme.spacing.lg
            spacing: Theme.spacing.md

            Button {
                text: qsTr("Remove link")
                // Greyed until there is a link: without one there is nothing to
                // revoke, and disableExport would succeed silently.
                enabled: !root.loading && root.link !== ""
                onClicked: {
                    root.close();
                    root.removeLinkRequested();
                }
            }

            Item {
                Layout.fillWidth: true
            }

            Button {
                text: qsTr("Copy link")
                highlighted: true
                enabled: !root.loading && root.link !== ""
                // Straight back through the controller rather than a clipboard
                // write here: Qt Quick exposes no clipboard API at all, and the
                // same call also raises the "Link copied" toast.
                onClicked: root.mutController.copyLinkToClipboard(root.handle)
            }

            Button {
                text: qsTr("Close")
                onClicked: root.close()
            }
        }
    }

    Connections {
        target: root.mutController

        function onLinkResolved(handle, link) {
            // Ignored for another node: the dialog can be closed and reopened
            // elsewhere while the first export is still in flight.
            if (handle !== root.handle)
                return;
            root.loading = false;
            root.link = link;
        }
    }
}
