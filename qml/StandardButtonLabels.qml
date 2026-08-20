pragma Singleton
import QtQuick
// No control is instantiated here, only DialogButtonBox's StandardButton enum
// is read, so the style import the visual files carry is not needed.
import QtQuick.Controls

// Dialog.standardButtons takes its button text from Qt's own translation
// catalogue, which QQmlApplicationEngine installs implicitly for the OS
// language -- so "Close" and "Cancel" came out Japanese in a UI that is English
// everywhere else. Every dialog that uses standardButtons calls pin() from
// Component.onCompleted to spell them out here instead. The hand-built button
// boxes (NewFolderDialog, NameConflictDialog, CopyConflictDialog) never had the
// problem because they already write their own qsTr() strings; this restores
// the same wording for the rest.
QtObject {
    function pin(box) {
        if (!box)
            return;
        pinOne(box, Dialog.Ok, qsTr("OK"));
        pinOne(box, Dialog.Cancel, qsTr("Cancel"));
        pinOne(box, Dialog.Close, qsTr("Close"));
        pinOne(box, Dialog.Yes, qsTr("Yes"));
        pinOne(box, Dialog.No, qsTr("No"));
    }

    // standardButton() returns null for a flag the dialog did not ask for, so
    // one list covers every dialog rather than each naming its own buttons.
    function pinOne(box, flag, label) {
        const button = box.standardButton(flag);
        if (button)
            button.text = label;
    }
}
