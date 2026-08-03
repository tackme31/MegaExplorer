import QtQuick
import QtQuick.Window
import QtQuick.Controls.FluentWinUI3

// The window's caption row, drawn by us instead of by Windows (Phase 17a).
// Registered with QWindowKit's WindowAgent as the title bar, so the OS still
// treats it as a real caption: drag to move, double-click to maximize/restore,
// right-click for the system menu are all handled by QWindowKit answering
// WM_NCHITTEST with HTCAPTION -- none of them are implemented here.
//
// The three system buttons must be registered via setSystemButton(), not just
// drawn: that registration is what makes Windows 11 pop the Snap Layout
// flyout when the maximize button is hovered.
//
// Since Phase 17b the tab strip rides here too, Explorer-11 style, which is
// why the width bookkeeping below exists: anything the strip covers stops
// being caption, so the strip must be kept off the right-hand end of the row.
Item {
    id: root

    // Passed down explicitly from Main.qml rather than looked up, matching how
    // the other components/ items take navController/dragProxy.
    required property WindowAgent windowAgent

    readonly property Window targetWindow: Window.window

    // Caption the tab strip may never eat into. With the strip up here this
    // band plus the gap the strip leaves below its own cap is the entire
    // draggable area of the window, so it can't be allowed to reach zero.
    readonly property int dragReserve: 120

    // Explorer 11's caption+tab row, and the exact sum of what TabStrip.qml
    // now pins down itself: TabBar topPadding 4 + TabButton 36 + TabBar
    // bottomPadding 0. Every one of those three is written explicitly over
    // there, so this is not a number to tune in isolation -- the zero bottom
    // padding is what lets the active tab's bottom edge sit flush with y=40 and
    // run into the toolbar row below it.
    //
    // It briefly was Fluent's own 48 (padding 4/4 around a 40px TabButton),
    // which showed the indicator correctly but left the row 8px thicker than
    // Explorer's.
    implicitHeight: 40

    // Called by Main.qml's own Component.onCompleted rather than run from
    // ours: every call below needs windowAgent.setup() to have happened
    // first, and Component.onCompleted fires innermost-object-first, so this
    // item (and its buttons) would otherwise register before the window's
    // handler ever runs.
    function registerWithAgent(): void {
    root.windowAgent.setTitleBar(root);
    root.windowAgent.setSystemButton(WindowAgent.Minimize, minimizeButton);
    root.windowAgent.setSystemButton(WindowAgent.Maximize, maximizeButton);
    root.windowAgent.setSystemButton(WindowAgent.Close, closeButton);
    tabStrip.registerWithAgent();
}

    // D3: the caption row is on the panel side of Explorer 11's two-surface
    // split, so it is the lighter of the pair in dark mode and the darker in
    // light. Until now this Item had no background at all and the window colour
    // showed through, making the row indistinguishable from the content.
    Rectangle {
        anchors.fill: parent
        color: Theme.color.surfaceAlt
    }

    TabStrip {
        id: tabStrip
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        // Content-driven rather than filling, and clamped so dragReserve
        // always survives: the strip's own rect is what the hit test carves
        // out of the caption, so a fillWidth strip would leave the window
        // undraggable once tabs were open.
        width: Math.max(0, Math.min(tabStrip.preferredWidth, root.width - systemButtons.width
                                    - root.dragReserve))
        // Tabs are logged-in chrome, but the caption row that carries them is
        // not (Main.qml keeps it outside the authState gate so LoginView can
        // still drag the window) -- hence a plain visible binding here rather
        // than a Loader. Hiding it hands the whole row back to dragging.
        visible: authController.authState === AuthController.LoggedIn
        windowAgent: root.windowAgent
    }

    // Stands in for the tab strip while logged out. Explorer 11 shows no
    // title text once tabs are on the caption row, and neither do we.
    Label {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.right: systemButtons.left
        anchors.rightMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        visible: !tabStrip.visible
        elide: Text.ElideRight
        text: root.targetWindow?.title ?? ""
    }

    Row {
        id: systemButtons
        anchors.right: parent.right
        anchors.top: parent.top
        height: parent.height

        CaptionButton {
            id: minimizeButton
            text: ""
            onClicked: root.targetWindow.showMinimized()
        }

        CaptionButton {
            id: maximizeButton
            readonly property bool isMaximized: root.targetWindow?.visibility === Window.Maximized

            text: maximizeButton.isMaximized ? "" : ""
            onClicked: maximizeButton.isMaximized ? root.targetWindow.showNormal() :
                                                    root.targetWindow.showMaximized()
        }

        CaptionButton {
            id: closeButton
            text: ""
            // Windows' own close button turns red on hover; the style has no
            // notion of that, so the background is replaced outright.
            background: Rectangle {
                color: closeButton.pressed ? Theme.color.closePressed : (closeButton.hovered ? Theme.color.closeHover : "transparent")
            }
            contentItem: Label {
                color: closeButton.hovered ? Theme.color.closeGlyphOn : palette.buttonText
                font: closeButton.font
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: closeButton.text
            }
            onClicked: root.targetWindow.close()
        }
    }

    // Segoe Fluent Icons is the Windows 11 system icon font; the four
    // glyphs used here carry the same code points in Windows 10's Segoe
    // MDL2 Assets, so both resolve without a fallback path.
    component CaptionButton: ToolButton {
        focusPolicy: Qt.NoFocus
        implicitWidth: 46
        implicitHeight: root.implicitHeight
        font.family: "Segoe Fluent Icons"
        font.pixelSize: 10
    }
}
