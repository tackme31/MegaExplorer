import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import MegaExplorer

// The right-hand preview pane: Main.qml's SplitView holds one of these for the
// whole window, and its visibility follows the active tab's previewVisible. The
// SplitView attached properties and the persisted-width read live in Main.qml
// beside the other two children, unlike SidePanel.qml -- this pane's width has to
// be re-applied every time it is shown, which is a SplitView-level concern.
//
// A Rectangle rather than a bare layout, for SidePanel.qml's reason: this is the
// lighter of Explorer 11's two surfaces, and a layout cannot paint one.
//
// controller and currentPane are untyped `var`: a TabContentPane-typed property
// would pull a views/ import into components/, reversing the direction those two
// depend on (StatusBar.qml states the same rule), and controller is injected
// rather than read from the context property so tst_PreviewPane.qml can supply a
// fake -- the QML test harness installs no context properties at all.
Rectangle {
    id: root

    required property var controller
    required property var currentPane

    readonly property var listModel: currentPane?.navController?.fileListModel ?? null

    color: Theme.color.surfaceAlt

    // The three things that decide what to show: which tab is active (listModel),
    // what is selected in it, and whether this pane is on at all. Only QML can see
    // all three, which is why the fetch starts here rather than in C++. Bumping the
    // generation on every call is also what makes a tab switch invalidate a fetch
    // still in flight for the tab left behind.
    function refresh() {
        // visible and listModel both change while this object is still being
        // built, before required properties have been assigned.
        if (!root.controller)
            return;
        if (!root.visible || !root.listModel) {
            root.controller.clear();
            return;
        }
        const entry = root.listModel.selectedEntry();
        if (!entry || entry.handle === undefined) {
            root.controller.clear();
            return;
        }
        root.controller.showSelection(entry.handle, entry.name, entry.sizeBytes, entry.isFolder);
    }

    onVisibleChanged: root.refresh()
    onListModelChanged: root.refresh()

    Connections {
        target: root.listModel
        function onSelectionChanged() {
            root.refresh();
        }
    }

    BusyIndicator {
        anchors.centerIn: parent
        running: visible
        visible: root.controller.state === PreviewController.Loading
    }

    Image {
        anchors.fill: parent
        anchors.margins: Theme.spacing.md
        visible: root.controller.state === PreviewController.Ready && root.controller.kind
                 === PreviewController.Image
        source: visible ? root.controller.imageSource : ""
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        // The URL carries a generation that never repeats, so a cache entry could
        // never be hit again -- it would only pin the bytes of every preview shown
        // this session.
        cache: false
        // A 1000px JPEG drawn at ~300px: without this the downscale is a plain
        // box filter and the result looks noticeably coarser than the thumbnail
        // grid's.
        mipmap: true
    }

    // The frame sits outside the flickable so the border doesn't scroll with the
    // text, and the flickable is explicit rather than a ScrollView because the
    // scroll position has to be resettable on every document swap -- ScrollView
    // owns its flickable privately. Both points are LicenseDialog.qml's, which
    // hit them first.
    Rectangle {
        anchors.fill: parent
        anchors.margins: Theme.spacing.md
        visible: root.controller.state === PreviewController.Ready && root.controller.kind
                 === PreviewController.Text
        color: Theme.color.fieldFill
        border.width: Theme.border.thin
        border.color: Theme.color.stroke
        radius: Theme.radius.sm

        Flickable {
            id: textFlick
            anchors.fill: parent
            anchors.margins: Theme.spacing.md
            clip: true
            contentWidth: width
            contentHeight: previewText.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            TextArea {
                id: previewText
                width: textFlick.width
                padding: 0
                background: null
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                font.family: "Consolas"
                font.pixelSize: Theme.font.caption
                // The pane must never take focus away from the file views, whose
                // arrow keys Main.qml's StackLayout hands back on every tab switch.
                activeFocusOnTab: false
            }
        }
    }

    // Assigned in three steps rather than bound, and all three matter. TextEdit
    // builds scene graph nodes only near its flickable's viewport, so replacing a
    // scrolled long document with a short one in a single pass leaves that node
    // range where the old document had it and the pane paints blank -- with
    // contentY, contentHeight and .text all reading correct. Clearing first drops
    // every node, the reset then happens against an empty document, and the real
    // text is laid out a frame later from the top. (LicenseDialog.qml records the
    // same three steps for the same reason.)
    function showText(value) {
        previewText.text = "";
        textFlick.contentY = 0;
        Qt.callLater(() => previewText.text = value);
    }

    Connections {
        target: root.controller
        function onChanged() {
            if (root.controller.kind === PreviewController.Text)
                root.showText(root.controller.text);
        }
    }

    Label {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.spacing.xl
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: Theme.color.textSecondary
        visible: root.controller.state === PreviewController.Unsupported
        text: switch (root.controller.reason) {
              case PreviewController.UnsupportedType:
                  return qsTr("No preview available for this file type");
              case PreviewController.TooLarge:
                  return qsTr("This file is too large to preview");
              case PreviewController.BinaryContent:
                  return qsTr("This file is not text");
              default:
                  return qsTr("No preview available");
              }
    }
}
