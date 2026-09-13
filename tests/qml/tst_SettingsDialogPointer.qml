import QtQuick
import QtTest
import MegaExplorer

// Real pointer delivery into SettingsDialog, which tst_MainDialogs.qml cannot do:
// its window is the default 200x200, so the dialog's rows lie outside it.
//
// Qt 6 keeps offering a press to pointer handlers below an item that accepted
// it, so a row TapHandler left on its default passive grab let the same click
// reach the file view's TapHandler behind the modal dialog and select a file.
TestCase {
    id: testCase
    name: "SettingsDialogPointer"
    width: 800
    height: 600
    visible: true
    when: windowShown

    // Stands in for FileViewInput's background TapHandler.
    Item {
        id: catcher
        property int taps: 0
        anchors.fill: parent
        TapHandler {
            onTapped: catcher.taps++
        }
    }

    Component {
        id: openWithStubComponent

        QtObject {
            property var stored: []
            function entries() {
                return stored;
            }
            function setEntries(list) {
            }
            function commandRunnable(command) {
                return true;
            }
            function commandWithProgram(command, url) {
                return command;
            }
        }
    }

    Component {
        id: settingsComponent
        SettingsDialog {}
    }

    function test_clickingAProgramRowStaysInTheDialog() {
        const stub = createTemporaryObject(openWithStubComponent, testCase, {
                                               "stored": [
                                                   {
                                                       "name": "A",
                                                       "extensions": "",
                                                       "commandLine": "a %U"
                                                   },
                                                   {
                                                       "name": "B",
                                                       "extensions": "",
                                                       "commandLine": "b %U"
                                                   }
                                               ]
                                           });
        const dialog = createTemporaryObject(settingsComponent, testCase, {
                                                 "openWith": stub
                                             });
        verify(dialog !== null);
        catcher.taps = 0;
        dialog.open();
        tryCompare(dialog, "opened", true);
        dialog.categoryList.currentIndex = 2; // Open with
        tryVerify(() => dialog.programList.itemAtIndex(1) !== null);
        const row = dialog.programList.itemAtIndex(1);
        waitForItemPolished(row);

        mouseClick(row);

        compare(dialog.programList.currentIndex, 1);
        compare(catcher.taps, 0);
    }
}
