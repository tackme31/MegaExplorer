import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml/NewFolderDialog.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts

// The full third-party license texts, master/detail: components on the left,
// the selected one's text on the right. Embedded in the binary rather than
// linked to, because BSD/Apache/GPL all require the license *copy* to travel
// with the distribution -- a URL does not satisfy that even for an app that
// cannot work offline.
//
// LicenseModel is a QML singleton reading the generated licenses/manifest.json
// out of qrc; scripts/gen_third_party_notices.py produces both it and the
// THIRD-PARTY-NOTICES.txt shipped beside the binary, from the same data.
Dialog {
    id: root

    parent: Overlay.overlay
    anchors.centerIn: Overlay.overlay
    modal: true
    title: qsTr("Open source licenses")
    standardButtons: Dialog.Close

    width: Math.min(Overlay.overlay.width * 0.9, 900)
    height: Math.min(Overlay.overlay.height * 0.85, 620)

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacing.md

        ListView {
            id: componentList

            Layout.preferredWidth: 260
            Layout.fillHeight: true
            clip: true
            model: LicenseModel
            currentIndex: 0
            ScrollBar.vertical: ScrollBar {}

            delegate: ItemDelegate {
                id: componentRow

                required property int index
                required property string name
                required property string version
                required property string license

                width: componentList.width
                highlighted: ListView.isCurrentItem
                onClicked: componentList.currentIndex = componentRow.index

                contentItem: ColumnLayout {
                    spacing: 0

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: componentRow.version ? `${componentRow.name} ${componentRow.version}` :
                                                     componentRow.name
                    }

                    Label {
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        font.pixelSize: Theme.font.caption
                        color: Theme.color.textSecondary
                        text: componentRow.license
                    }
                }
            }
        }

        Rectangle {
            Layout.preferredWidth: Theme.border.thin
            Layout.fillHeight: true
            color: Theme.color.stroke
        }

        // The frame is drawn here rather than by the TextArea's own background,
        // which would sit inside the flickable and scroll away with the text.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.color.fieldFill
            border.color: Theme.color.stroke
            border.width: Theme.border.thin
            radius: Theme.radius.sm

            // An explicit Flickable rather than a ScrollView: switching
            // components has to reset the scroll offset, and ScrollView owns its
            // flickable privately, so there is nothing dependable to reset.
            Flickable {
                id: textFlick

                anchors.fill: parent
                anchors.margins: Theme.spacing.md
                clip: true
                contentWidth: width
                contentHeight: licenseText.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                TextArea {
                    id: licenseText

                    width: textFlick.width
                    padding: 0
                    background: null
                    readOnly: true
                    // Copyable on purpose: the point of the dialog is that the
                    // user can actually obtain these texts.
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    font.family: "Consolas"
                    font.pixelSize: Theme.font.caption
                }
            }
        }
    }

    // Assigned, not bound: licenseText() reads a file, and several hundred KB of
    // text must not be re-fetched every time an unrelated dependency changes.
    function showLicense(row: int) {
        // Three steps, and all three are load-bearing. TextEdit builds scene
        // graph nodes only for the stretch of text near its flickable's
        // viewport; replacing a scrolled 34KB document with a short one in a
        // single pass leaves that node range where the old document had it and
        // the pane paints blank -- with contentY, contentHeight and .text all
        // reading correct, and no later scroll bringing it back. Clearing first
        // drops every node, the reset then happens against an empty document,
        // and the real text is laid out a frame later from the top.
        licenseText.text = "";
        textFlick.contentY = 0;
        Qt.callLater(() => licenseText.text = LicenseModel.licenseText(row));
    }

    Connections {
        target: componentList

        function onCurrentIndexChanged() {
            root.showLicense(componentList.currentIndex);
        }
    }

    // currentIndex starts at 0 and never "changes", so the first text needs
    // fetching explicitly.
    onOpened: root.showLicense(componentList.currentIndex)
}
