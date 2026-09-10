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

    // The expiry MEGA holds for this link: -1 no link at all, 0 never expires,
    // otherwise Unix seconds. Read locally (no round trip), so it is right before
    // the export even lands.
    property real expiry: -1
    property bool expiryBusy: false
    property string expiryError: ""

    // Bound to accountController by whoever declares this dialog, rather than read
    // off the root context here, so the QML test can instantiate it without
    // main.cpp's context properties -- the same arrangement CopyConflictDialog uses.
    // AccountController::PlanLevel: -1 until the account read lands, 0 free.
    property int planLevel: -1

    // Expiry dates are a MEGA plan feature: the SDK refuses them with kEAccess on a
    // free account (STUDY_PUBLIC_LINK_SETTINGS section 1.2). Withheld while the plan
    // is still unknown as well, so a free account is never briefly offered a control
    // that would fail.
    readonly property bool expiryAllowed: root.planLevel > 0
    readonly property bool expiryEditable: !root.loading && root.link !== "" && !root.expiryBusy
                                           && root.expiryAllowed

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
        root.expiryBusy = false;
        root.open();
        root.showExpiry(root.mutController.linkExpiry(root.handle));
        root.mutController.requestLink(root.handle);
    }

    // The one place the expiry row is written from, so the checkbox and the field
    // never disagree. The checkbox's `checked` is assigned rather than bound: a
    // click writes that property itself, which would drop a binding and leave the
    // box stuck on the user's guess the next time MEGA refused the change.
    function showExpiry(seconds: real) {
        root.expiry = seconds;
        root.expiryError = "";
        expiryToggle.checked = seconds > 0;
        expiryField.text = seconds > 0 ? Qt.formatDate(new Date(seconds * 1000), "yyyy-MM-dd") : "";
    }

    // Start of the chosen day in local time, which is what MEGA's own web client
    // sends for that date -- 23:59:59 would put the two a day apart on one date.
    function startOfDay(day: date): real {
        return Math.floor(
            new Date(day.getFullYear(), day.getMonth(), day.getDate()).getTime() / 1000);
    }

    // Deliberately not Date.fromLocaleDateString: that accepts sloppy input and
    // rolls overflow over silently, so "2026-02-31" would come back as 3 March
    // rather than as the typo it is. The round-trip check below is what rejects it.
    function parseDate(text: string) {
        const parts = /^(\d{4})-(\d{2})-(\d{2})$/.exec(text.trim());
        if (parts === null)
            return null;
        const year = parseInt(parts[1], 10);
        const month = parseInt(parts[2], 10) - 1;
        const day = parseInt(parts[3], 10);
        const parsed = new Date(year, month, day);
        if (parsed.getFullYear() !== year || parsed.getMonth() !== month
                || parsed.getDate() !== day)
            return null;
        return parsed;
    }

    function requestExpiry(seconds: real) {
        root.expiryError = "";
        root.expiryBusy = true;
        root.mutController.setLinkExpiry(root.handle, seconds);
    }

    function applyTypedDate() {
        if (!expiryToggle.checked || root.expiryBusy)
            return;
        const parsed = root.parseDate(expiryField.text);
        if (parsed === null) {
            root.expiryError = qsTr("Enter a date as YYYY-MM-DD.");
            return;
        }
        const seconds = root.startOfDay(parsed);
        if (seconds * 1000 <= Date.now()) {
            root.expiryError = qsTr("Pick a date in the future.");
            return;
        }
        // editingFinished also fires on focus loss, so an untouched field would
        // otherwise re-issue the link every time the dialog was closed.
        if (seconds === root.expiry) {
            root.expiryError = "";
            return;
        }
        root.requestExpiry(seconds);
    }

    // Why the row is unusable, for the tooltip rather than a caption line: it
    // only ever has something to say while the control is off, so as a caption
    // it would appear and vanish and shift everything under it.
    function expiryHint(): string {
        if (root.planLevel < 0)
            return qsTr("Checking your plan…");
        if (!root.expiryAllowed)
            return qsTr("Expiry dates need a Pro plan.");
        return "";
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

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacing.xs
            implicitHeight: Theme.border.thin
            color: Theme.color.stroke
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.md

            // The switch is wrapped rather than carrying the MouseArea itself:
            // `enabled` propagates down, so a hover target inside a disabled
            // Switch is disabled too and the tooltip would never appear.
            Item {
                implicitWidth: expiryToggle.implicitWidth
                implicitHeight: expiryToggle.implicitHeight

                Switch {
                    id: expiryToggle
                    text: qsTr("Expiry date")
                    enabled: root.expiryEditable
                    // Turning it on has to name a day, and applying happens on
                    // the click (there is no save button), so a week out is the
                    // value offered -- the field is editable straight after.
                    onToggled: root.requestExpiry(
                                   checked ? root.startOfDay(new Date(Date.now() + 7 * 86400 * 1000)) : 0)
                }

                // Disabled items get no hover events, so the reason the row is
                // off has to be heard by something outside it. Disabled while
                // the switch works, which lets the clicks through to it.
                MouseArea {
                    anchors.fill: parent
                    enabled: !expiryToggle.enabled && root.expiryHint() !== ""
                    hoverEnabled: true
                    ToolTip.text: root.expiryHint()
                    ToolTip.delay: 500
                    ToolTip.visible: containsMouse
                }
            }

            Item {
                Layout.fillWidth: true
            }

            TextField {
                id: expiryField
                visible: expiryToggle.checked
                enabled: root.expiryEditable
                implicitWidth: 110
                placeholderText: "YYYY-MM-DD"
                onEditingFinished: root.applyTypedDate()
            }
        }

        Label {
            Layout.fillWidth: true
            visible: text !== ""
            wrapMode: Text.Wrap
            font.pixelSize: Theme.font.caption
            color: Theme.color.danger
            text: root.expiryError
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
            // Re-read rather than trusted from before the export: the node may not
            // have had a link at all when the dialog opened, and only now does it
            // carry the expiry the row has to show.
            root.showExpiry(root.mutController.linkExpiry(handle));
        }

        function onLinkExpiryResolved(handle, expiry) {
            if (handle !== root.handle)
                return;
            root.expiryBusy = false;
            // Carries the value MEGA actually holds, so a refusal puts the row back
            // where it was instead of leaving it on what the user asked for. The
            // reason is on a toast; showExpiry clears the inline message with it.
            root.showExpiry(expiry);
        }
    }
}
