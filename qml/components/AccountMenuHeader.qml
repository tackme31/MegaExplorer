import QtQuick
import QtQuick.Effects
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml/AboutDialog.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts

// The account block at the top of Main.qml's "More" menu: avatar, display
// name, email, and the storage-usage bar with the plan name.
//
// Declared as a plain Item, never a Control or AbstractButton. Menu accepts
// any item as a child, and its Up/Down walk skips whatever is not
// tab-focusable -- which is exactly how the MenuSeparator below this is
// already skipped. Being a bare Item (plus the explicit activeFocusOnTab
// below) is what keeps this block out of the keyboard order.
//
// Reads accountController straight off the root context, like every other
// component here reads tabsController/uploadController; nothing is passed in.
Item {
    id: root

    activeFocusOnTab: false

    // Only a floor. A Menu's contentItem is a ListView, which does not
    // aggregate its children's implicitWidth, so this does not widen the menu
    // -- Main.qml sets the menu's width explicitly instead, and this item is
    // stretched to it.
    implicitWidth: 280
    implicitHeight: layout.implicitHeight + Theme.spacing.lg * 2

    // Plan names are labels, so they live in QML rather than C++ (the Phase 19
    // MenuAction/ActionCatalog split: C++ owns which values exist, QML owns
    // what they are called). One consumer, so a local function beats a
    // singleton. The default arm must stay printable -- ACCOUNT_TYPE_FEATURE
    // and anything MEGA adds later land there.
    function planName(level) {
        switch (level) {
        case AccountController.Free:
            return qsTr("Free");
        case AccountController.ProI:
            return qsTr("Pro I");
        case AccountController.ProII:
            return qsTr("Pro II");
        case AccountController.ProIII:
            return qsTr("Pro III");
        case AccountController.Lite:
            return qsTr("Pro Lite");
        case AccountController.Starter:
            return qsTr("Starter");
        case AccountController.Basic:
            return qsTr("Basic");
        case AccountController.Essential:
            return qsTr("Essential");
        case AccountController.Business:
            return qsTr("Business");
        case AccountController.ProFlexi:
            return qsTr("Pro Flexi");
        default:
            return qsTr("MEGA");
        }
    }

    // Positioned rather than anchors.fill'd: filling the parent would make the
    // layout's height depend on the parent's, whose implicitHeight already
    // depends on the layout's, and that round trip needs a settle frame.
    ColumnLayout {
        id: layout

        x: Theme.spacing.lg
        y: Theme.spacing.lg
        width: parent.width - Theme.spacing.lg * 2
        spacing: Theme.spacing.sm

        Item {
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: Theme.spacing.xs
            implicitWidth: Theme.avatar.size
            implicitHeight: Theme.avatar.size

            // Fallback whenever there is no avatar file, and also when one
            // arrived but failed to decode -- otherwise a truncated JPEG would
            // leave a hole where the portrait should be.
            readonly property bool showFallback: accountController.avatarUrl == ""
                                                 || avatarImage.status === Image.Error

            Rectangle {
                anchors.fill: parent
                visible: parent.showFallback
                radius: width / 2
                color: accountController.avatarColor !== "" ? accountController.avatarColor :
                                                              Theme.color.accent

                Label {
                    anchors.centerIn: parent
                    text: accountController.avatarInitial
                    color: "#ffffff" // the SDK's avatar colours are picked for white text
                    font.pixelSize: Theme.avatar.initialFontSize
                }
            }

            Image {
                id: avatarImage

                anchors.fill: parent
                visible: false // only ever drawn through the MultiEffect below
                source: accountController.avatarUrl
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                // Image caches by URL, and re-logging into the same account
                // rewrites the same path -- without this the stale pixmap wins.
                cache: false
                sourceSize.width: Theme.avatar.size * 2
                sourceSize.height: Theme.avatar.size * 2
            }

            // Rectangle { radius; clip: true } does NOT produce a circle: Qt
            // Quick's clip is a rectangular scissor and ignores radius. Masking
            // is the only way to round an Image.
            Rectangle {
                id: avatarMask

                anchors.fill: parent
                visible: false
                layer.enabled: true
                radius: width / 2
                color: "black"
            }

            MultiEffect {
                anchors.fill: parent
                visible: !parent.showFallback
                source: avatarImage
                maskEnabled: true
                maskSource: avatarMask
            }
        }

        // Never hidden, even with no name to show. The whole block's height
        // must be fixed from the moment it is created: FluentWinUI3's Menu
        // animates its height on open, and an item that grows part-way through
        // that animation makes it lurch (measured: the menu snapped 204 -> 279
        // -> 218 -> 279 when this label appeared ~270ms in). The display name
        // arrives asynchronously, so reserving its line is the only way to
        // keep the height constant. Same reasoning as the storage label below.
        Label {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            text: accountController.displayName
            elide: Text.ElideRight
            font.bold: true
        }

        Label {
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.spacing.sm
            horizontalAlignment: Text.AlignHCenter
            text: accountController.email
            elide: Text.ElideRight
            font.pixelSize: Theme.font.caption
            color: Theme.color.textSecondary
        }

        // One block for all three storage states, never swapped for another.
        // An earlier version toggled this against a separate failed-state
        // block, but the two had different heights, which is the same hazard
        // the display-name label above documents.
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.xs

            // The track is always drawn, so the block keeps its height from
            // the moment the menu opens and nothing shifts when the numbers
            // land. The fill only exists once they have: a zero-width fill
            // would assert "nothing used", which is not what is known yet.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: Theme.storageBar.height
                radius: height / 2
                color: Theme.color.storageTrack

                Rectangle {
                    visible: accountController.storageState === AccountController.Loaded
                    width: parent.width * accountController.storageRatio
                    height: parent.height
                    radius: parent.radius
                    color: accountController.storageRatio > 0.9 ? Theme.color.danger :
                                                                  Theme.color.accent
                }
            }

            // Carries all three states in one always-present line, so the
            // block's height never changes. Empty while loading -- an empty
            // Label still occupies a line, which is what reserves the space.
            // Do not "fix" this with a hardcoded height or a visible binding.
            //
            // The failed state puts its retry link here too, rather than in a
            // block of its own. StyledText gets the system link colour and
            // hand cursor for free, same as AboutDialog.
            Label {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.font.caption
                color: Theme.color.textSecondary
                elide: Text.ElideRight
                textFormat: Text.StyledText
                text: {
                    switch (accountController.storageState) {
                    case AccountController.Loaded:
                        return qsTr("%1 · %2").arg(accountController.storageText).arg(root.planName(
                                                                                          accountController.planLevel));
                    case AccountController.Failed:
                        return qsTr("Storage unavailable. <a href=\"retry\">Retry</a>");
                    default:
                        return "";
                    }
                }
                onLinkActivated: accountController.retryAccountInfo()
            }
        }
    }
}
