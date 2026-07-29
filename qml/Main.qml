import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls").
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QtCore
// Directory import for DownloadSnackbar.qml -- the CMake-generated qmldir
// merge (QTP0004) resolves this at build time regardless, but static tooling
// (Qt Creator's classic QML/JS model, qmllint without the build dir) only
// knows about the plain-QML directory-import mechanism, not that mechanism.
import "components"
// Directory import for FileTableView.qml -- same QTP0004 caveat as the
// "components" import above (static tooling needs this explicit import even
// though the CMake-generated qmldir merge resolves it either way).
import "views"

ApplicationWindow {
    id: window
    width: 1200
    height: 800
    minimumWidth: 200
    minimumHeight: 250
    visible: true
    title: qsTr("MegaExplorer")

    // 0 = list, 1 = grid. Persisted below via Settings (alias, so every
    // change is written through automatically -- a plain property on
    // Settings would only capture the value at startup).
    property int viewMode: 0

    Settings {
        property alias viewMode: window.viewMode
        property alias windowWidth: window.width
        property alias windowHeight: window.height
    }

    function activateEntry(isFolder, handle, name, sizeBytes) {
        if (isFolder)
            controller.openFolder(handle);
        else
            downloadController.downloadFile(handle, name, sizeBytes);
    }

    // Logged-in chrome (header/footer/central StackLayout) only exists while
    // authController.authState === LoggedIn; otherwise the window shows just
    // LoginView, no header/footer. Header/footer are Loader-driven (Loader
    // can stand in directly for ApplicationWindow's header:/footer: slot
    // items); the central area is a third Loader switching between the
    // logged-in StackLayout and LoginView. This codebase's first Loader use
    // -- an exclusive two-state switch, not a multi-step screen flow, so
    // Loader rather than StackView.
    header: Loader {
        active: authController.authState === AuthController.LoggedIn
        sourceComponent: headerComponent
    }

    footer: Loader {
        active: authController.authState === AuthController.LoggedIn
        sourceComponent: footerComponent
    }

    Component {
        id: headerComponent

        ToolBar {
            RowLayout {
                anchors.fill: parent

                ToolButton {
                    text: qsTr("← Back")
                    enabled: controller.canGoBack
                    // Without this, clicking here while the grid is showing
                    // (view mode doesn't change, so no StackLayout focus
                    // handoff fires) leaves focus on the button and arrow
                    // keys dead until the view is re-clicked.
                    focusPolicy: Qt.NoFocus
                    onClicked: controller.goBack()
                }

                // 7:3 against the search field below. Qt Quick Layouts
                // distributes space between fillWidth items in the ratio of
                // their preferred sizes ("If there are multiple items with
                // fillWidth set to true, the layout will grow or shrink the
                // items relative to the ratio of their preferred size" --
                // Qt 6.11 Layout docs), so the literal 7/3 below are that
                // ratio, not pixel values. minimumWidth: 0 is spelled out
                // (it's already the default for a non-layout item) because
                // "no minimum width" is a deliberate requirement here.
                Breadcrumb {
                    model: controller.breadcrumb
                    Layout.fillWidth: true
                    Layout.preferredWidth: 7
                    Layout.minimumWidth: 0
                    Layout.fillHeight: true
                }

                TextField {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 3
                    Layout.minimumWidth: 0
                    placeholderText: qsTr("Search in this folder")
                    // MegaApi::search() blocks the GUI thread synchronously, so
                    // search on Enter only rather than on every keystroke.
                    onAccepted: controller.search(text)
                }

                ToolButton {
                    text: "≡"
                    focusPolicy: Qt.NoFocus
                    onClicked: signOutMenu.popup()

                    Menu {
                        id: signOutMenu
                        MenuItem {
                            text: qsTr("Sign out")
                            onTriggered: signOutConfirmDialog.open()
                        }
                    }
                }
            }
        }
    }

    Component {
        id: footerComponent

        ToolBar {
            RowLayout {
                anchors.fill: parent

                Label {
                    Layout.fillWidth: true
                    visible: downloadController.downloadActive
                    elide: Text.ElideMiddle
                    text: downloadController.activeFileName
                }
                ProgressBar {
                    Layout.preferredWidth: 160
                    visible: downloadController.downloadActive
                    from: 0
                    to: 1
                    value: downloadController.activeProgress
                }

                // Keeps the view-mode buttons right-aligned when the download
                // group above is hidden (RowLayout excludes invisible items).
                Item {
                    Layout.fillWidth: true
                    visible: !downloadController.downloadActive
                }

                ToolButton {
                    text: "☰"
                    checkable: true
                    checked: window.viewMode === 0
                    // Clicking ⊞->☰ while already on the grid changes
                    // viewMode, which does trigger the StackLayout focus
                    // handoff -- but the symmetric case (clicking ☰ while
                    // already on the list) doesn't, so both buttons need
                    // this for consistency.
                    focusPolicy: Qt.NoFocus
                    onClicked: window.viewMode = 0
                }
                ToolButton {
                    text: "⊞"
                    checkable: true
                    checked: window.viewMode === 1
                    focusPolicy: Qt.NoFocus
                    onClicked: window.viewMode = 1
                }
            }
        }
    }

    Dialog {
        id: signOutConfirmDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        standardButtons: Dialog.Yes | Dialog.Cancel
        // DownloadService has no cancel API yet, so an in-flight transfer is
        // simply aborted by logout(). Warn about it up front rather than
        // silently dropping it.
        title: downloadController.downloadActive ? qsTr("Sign out? (download in progress)") : qsTr(
                                                       "Sign out?")
        onAccepted: authController.logout()
    }

    Loader {
        anchors.fill: parent
        sourceComponent: authController.authState === AuthController.LoggedIn
                         ? mainContentComponent : loginComponent
    }

    Component {
        id: loginComponent

        LoginView {}
    }

    Component {
        id: mainContentComponent

        StackLayout {
            anchors.fill: parent
            currentIndex: window.viewMode

            FileTableView {
                id: fileTableView
                // StackLayout keeps both children alive and just toggles
                // visible, and an invisible item can't hold activeFocus -- so
                // focus has to be handed back explicitly on every switch, or
                // arrow keys go dead until the view is re-clicked.
                // Qt.callLater defers past StackLayout's own visibility
                // update, which runs after this notification fires.
                StackLayout.onIsCurrentItemChanged: if (StackLayout.isCurrentItem)
                                                        Qt.callLater(()
                                                                     => fileTableView.forceActiveFocus(
                                                                            ))
                onActivateRequested: (isFolder, handle, name, sizeBytes) => window.activateEntry(
                                                                                isFolder, handle,
                                                                                name, sizeBytes)
            }

            GridView {
                id: gridView
                model: controller.fileListModel
                clip: true
                cellWidth: 120
                cellHeight: 120
                // GridView has its own built-in arrow-key handling
                // (currentIndex movement + auto-scroll) that would otherwise
                // fight with the selection model driven by Keys.onPressed
                // below.
                keyNavigationEnabled: false
                StackLayout.onIsCurrentItemChanged: if (StackLayout.isCurrentItem)
                                                        Qt.callLater(() => gridView.forceActiveFocus(
                                                                               ))

                Keys.onPressed: event => {
                    if (event.modifiers & Qt.AltModifier)
                        return; // reserved for a future Alt+Left "back" shortcut

                    if (event.matches(StandardKey.SelectAll)) {
                        controller.fileListModel.selectAll();
                        event.accepted = true;
                        return;
                    }

                    // Matches GridView's own FlowLeftToRight layout math; no
                    // ScrollBar is attached, so width is the full viewport
                    // width. Recomputed per key press rather than cached, so
                    // a window resize doesn't need separate handling.
                    const columns = Math.max(1, Math.floor(gridView.width / gridView.cellWidth));
                    let delta = 0;
                    if (event.key === Qt.Key_Left)
                        delta = -1;
                    else if (event.key === Qt.Key_Right)
                        delta = 1;
                    else if (event.key === Qt.Key_Up)
                        delta = -columns;
                    else if (event.key === Qt.Key_Down)
                        delta = columns;
                    else
                        return;

                    controller.fileListModel.moveCursor(delta, event.modifiers);
                    const row = controller.fileListModel.cursorRow();
                    if (row >= 0)
                        gridView.positionViewAtIndex(row, GridView.Contain);
                    event.accepted = true;
                }

                SystemPalette {
                    id: sysPalette
                }

                // Same rationale as FileTableView.qml's -- see the comment there for why
                // this handler is re-parented to the view and why it owns the selection.
                TapHandler {
                    parent: gridView
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        gridView.forceActiveFocus();
                        const pos = gridView.contentItem.mapFromItem(gridView, point.position);
                        const idx = gridView.indexAt(pos.x, pos.y);
                        if (idx < 0)
                            controller.fileListModel.clearSelection();
                        else
                            controller.fileListModel.selectRow(idx, point.modifiers);
                    }
                }

                delegate: Item {
                    id: gridDelegateItem
                    required property int index
                    required property string name
                    required property bool isFolder
                    required property var handle
                    required property var sizeBytes
                    required property bool hasThumbnail
                    required property string thumbnailPath
                    required property bool selected

                    width: GridView.view.cellWidth
                    height: GridView.view.cellHeight

                    Component.onCompleted: {
                        if (gridDelegateItem.hasThumbnail && !gridDelegateItem.isFolder)
                            thumbnailController.requestThumbnail(gridDelegateItem.handle);
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: 4
                        color: gridDelegateItem.selected ? Qt.rgba(sysPalette.highlight.r,
                                                                   sysPalette.highlight.g,
                                                                   sysPalette.highlight.b, 0.35) :
                                                           "transparent"
                    }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 2

                        Item {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            Image {
                                anchors.fill: parent
                                visible: gridDelegateItem.hasThumbnail &&
                                         !gridDelegateItem.isFolder
                                         && gridDelegateItem.thumbnailPath !== ""
                                // thumbnailPath uses native (backslash-on-Windows)
                                // separators -- normalize before building a URL.
                                source: gridDelegateItem.thumbnailPath ? ("file:///"
                                                                          + gridDelegateItem.thumbnailPath.replace(
                                                                              /\\/g, "/")) : ""
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                            }

                            Label {
                                anchors.centerIn: parent
                                visible: !gridDelegateItem.hasThumbnail
                                         || gridDelegateItem.isFolder
                                         || gridDelegateItem.thumbnailPath === ""
                                text: gridDelegateItem.isFolder ? "📁" : "📄"
                                font.pixelSize: 32
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideMiddle
                            text: gridDelegateItem.name
                        }
                    }

                    // Left-click selection is handled entirely by the view-level
                    // background TapHandler above (see its comment) -- this one is
                    // double-click-only.
                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        onDoubleTapped: window.activateEntry(gridDelegateItem.isFolder,
                                                             gridDelegateItem.handle,
                                                             gridDelegateItem.name,
                                                             gridDelegateItem.sizeBytes)
                    }

                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: {
                            if (!gridDelegateItem.selected) {
                                gridView.forceActiveFocus();
                                controller.fileListModel.selectRow(gridDelegateItem.index,
                                                                   Qt.NoModifier);
                            }
                            if (!gridDelegateItem.isFolder)
                                gridContextMenu.popup();
                        }
                    }

                    FileContextMenu {
                        id: gridContextMenu
                        delegateItem: gridDelegateItem
                    }
                }
            }
        } // StackLayout
    } // mainContentComponent

    DownloadSnackbar {
        id: downloadSnackbar
        parent: Overlay.overlay
    }

    ErrorToast {
        id: errorToast
        parent: Overlay.overlay
    }

    Connections {
        target: downloadController
        function onDownloadFinished(success, fileName, localPath, errorMessage, alreadyPresent) {
            downloadSnackbar.show(success, fileName, localPath, errorMessage, alreadyPresent);
        }
    }

    Connections {
        target: notificationController
        function onErrorOccurred(context, errorMessage) {
            errorToast.show(context, errorMessage);
        }
    }

    Connections {
        target: authController
        function onAuthStateChanged() {
            if (authController.authState === AuthController.LoggedIn)
                controller.loadRoot();
            else if (authController.authState === AuthController.LoggedOut)
                controller.reset();
        }
    }
}
