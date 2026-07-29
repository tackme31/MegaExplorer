import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3

// Windows-Explorer-style breadcrumb trail. model is
// FolderNavigationController::breadcrumb -- a QVariantList of
// {name, handle, isRoot}, root-first, current folder last. Each delegate is
// a label (clickable, navigates there) plus its own independent separator
// ">" element -- kept separate now so a later phase can attach a
// TapHandler/Menu to just the separator (subfolder dropdown) without
// restructuring the delegate.
//
// Overflow: when the trail doesn't fit root.width, segments are hidden from
// the left (root) side and replaced with a leading "«". The cutoff index is
// computed directly from each delegate's implicitWidth (stable regardless of
// visible state) rather than by iteratively toggling visible and re-reading
// Row.implicitWidth, which wouldn't converge in one pass.
Item {
    id: root
    clip: true

    property alias model: repeater.model

    implicitHeight: row.implicitHeight

    // Index of the first (leftmost) segment still shown; segments before it
    // are hidden and represented by the "«" indicator. The last segment
    // (current folder) is always kept, eliding its label instead if needed.
    property int firstVisibleIndex: 0

    function relayout() {
        const n = repeater.count;
        if (n === 0) {
            root.firstVisibleIndex = 0;
            return;
        }

        let idx = 0;
        while (idx < n - 1) {
            let total = (idx > 0 ? overflowIndicator.implicitWidth : 0);
            for (let i = idx; i < n; ++i) {
                const item = repeater.itemAt(i);
                if (item)
                    total += item.implicitWidth;
            }
            if (total <= root.width)
                break;
            ++idx;
        }
        root.firstVisibleIndex = idx;
    }

    onWidthChanged: Qt.callLater(root.relayout)

    Row {
        id: row
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter

        Label {
            id: overflowIndicator
            visible: root.firstVisibleIndex > 0
            verticalAlignment: Text.AlignVCenter
            leftPadding: 4
            rightPadding: 2
            text: "«"
        }

        Repeater {
            id: repeater
            onItemAdded: Qt.callLater(root.relayout)
            onItemRemoved: Qt.callLater(root.relayout)

            delegate: Row {
                id: delegateRoot
                required property int index
                required property var modelData

                visible: delegateRoot.index >= root.firstVisibleIndex

                Label {
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 4
                    rightPadding: 2
                    elide: Text.ElideRight
                    text: delegateRoot.modelData.isRoot ? qsTr("Cloud Drive") :
                                                          delegateRoot.modelData.name

                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        // The last segment is the current folder -- nothing
                        // to navigate to.
                        enabled: delegateRoot.index < repeater.count - 1
                        onTapped: controller.navigateTo(delegateRoot.modelData.handle,
                                                        delegateRoot.modelData.isRoot)
                    }
                }

                // Independent element (not part of the label above) so a
                // later phase can hang a subfolder-listing TapHandler/Menu
                // off just this separator without touching the label.
                Label {
                    visible: delegateRoot.index < repeater.count - 1
                    verticalAlignment: Text.AlignVCenter
                    text: ">"
                }
            }
        }
    }

    Connections {
        target: repeater
        function onModelChanged() {
            Qt.callLater(root.relayout);
        }
    }
}
