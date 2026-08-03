import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls").
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QtCore
// Directory import for DownloadSnackbar.qml/TabStrip.qml -- the CMake-generated
// qmldir merge (QTP0004) resolves this at build time regardless, but static
// tooling (Qt Creator's classic QML/JS model, qmllint without the build dir)
// only knows about the plain-QML directory-import mechanism, not that
// mechanism.
import "components"
// Directory import for TabContentPane.qml -- same QTP0004 caveat as the
// "components" import above (static tooling needs this explicit import even
// though the CMake-generated qmldir merge resolves it either way).
import "views"

ApplicationWindow {
    id: window
    width: 1200
    height: 800
    minimumWidth: 200
    minimumHeight: 250
    // Shown from Component.onCompleted below, not declaratively: QWindowKit's
    // setup() drops the native caption via WM_NCCALCSIZE, which hands those
    // ~31px back to the client area. Doing that to an already-visible window
    // grows window.height by exactly that much, and the Settings alias below
    // then persists the inflated value -- the window gained 31px on every
    // launch until this was deferred.
    visible: false
    title: qsTr("MegaExplorer")
    // The file-list surface (D3). Set on the window rather than per view
    // because every region on that side of the UI -- both file views, the tab
    // pane, the breadcrumb, LoginView -- draws no background of its own and
    // falls through to here. The two chrome regions that must not be this
    // colour (side panel, footer) paint themselves.
    color: Theme.color.surface

    // QWindowKit's window agent (Phase 17a): takes the native caption away
    // from Windows and hands it to captionBar below, while keeping the real
    // Win32 behaviour (Snap Layout flyout, DWM animations, shadow, rounded
    // corners) that a plain Qt.FramelessWindowHint would throw away.
    //
    // Named winAgent, not windowAgent, purely to keep CaptionBar's
    // `windowAgent: ...` assignment below from resolving to CaptionBar's own
    // property of that name instead of this id.
    WindowAgent {
        id: winAgent
    }

    // QWindowKit hardcodes dark-mode = true for the native 1px window border
    // (windows10borderhandler_p.h's setupNecessaryAttributes), so in the light
    // theme the frame stays dark unless we override it per scheme. Must run
    // after setup() -- the agent's context does not exist before that.
    function applyFrameTheme(): void {
    winAgent.setWindowAttribute("dark-mode", !Theme.isLight);
}

    Connections {
        target: Theme
        function onIsLightChanged() {
            window.applyFrameTheme();
        }
    }

    // setup() must precede every setTitleBar/setSystemButton call -- the
    // agent's internal context is only created here, and the register calls
    // dereference it unguarded. Component.onCompleted fires innermost-first,
    // so captionBar can't do its own registration from its onCompleted; it
    // exposes registerWithAgent() and this root handler drives the order.
    Component.onCompleted: {
        winAgent.setup(window);
        captionBar.registerWithAgent();
        window.applyFrameTheme();

        // Showing a frameless window hands the pixels the native caption used
        // to occupy back to the client area, so window.height jumps by the
        // caption height (~31px) the moment visible turns true -- and the
        // Settings alias above persists that, making the window taller on
        // every single launch. The jump is synchronous with the assignment,
        // so restoring the pre-show size right here settles it in one turn,
        // before the frame is presented.
        const restoredWidth = window.width;
        const restoredHeight = window.height;
        window.visible = true;
        window.width = restoredWidth;
        window.height = restoredHeight;
    }

    // 0 = list, 1 = grid. Persisted below via Settings (alias, so every
    // change is written through automatically -- a plain property on
    // Settings would only capture the value at startup). This -- together
    // with sortColumn/sortAscending/columnWidth* below -- is the single
    // app-wide last-write-wins value: any tab's TabContentPane writes here
    // the moment it changes (see TabContentPane.qml's viewModeWriteBack),
    // and a brand-new tab reads it back as its own starting point (see the
    // Repeater delegate's initialViewMode below). N tabs each owning their
    // own Settings item would instead fight over the same registry keys, so
    // this single copy lives here, not per-view (see
    // FileTableView.qml/TabContentPane.qml's own comments on this).
    property int viewMode: 0
    property int sortColumn: 0
    property bool sortAscending: true
    property real columnWidthName: -1
    property real columnWidthModified: -1
    property real columnWidthSize: -1

    // Phase 10 side panel: shared chrome beside the tab content, not
    // per-tab state (unlike viewMode/sortColumn/etc. above, which are each
    // tab's own last-write-wins starting point). Width is read once,
    // imperatively, by mainContentComponent's Component.onCompleted below --
    // same "one-shot read, not a live binding" convention as
    // TabContentPane.qml's initialViewMode (see its own comment for why).
    property real treePanelWidth: 240

    // The currently active tab's TabContentPane, kept in sync by the Binding
    // inside mainContentComponent below. footerComponent (a sibling nested
    // Component, so it can't see mainContentComponent's internal ids
    // directly) reads/writes this to drive the view-mode toggle buttons
    // against whichever tab is actually showing.
    property var currentPane: null

    Settings {
        property alias viewMode: window.viewMode
        property alias windowWidth: window.width
        property alias windowHeight: window.height
        property alias sortColumn: window.sortColumn
        property alias sortAscending: window.sortAscending
        property alias columnWidthName: window.columnWidthName
        property alias columnWidthModified: window.columnWidthModified
        property alias columnWidthSize: window.columnWidthSize
        property alias treePanelWidth: window.treePanelWidth
    }

    // Toolbar-row button: a square, glyph-only ToolButton with a tooltip
    // standing in for the label it no longer has (S7). Declared inline rather
    // than as its own file -- same call as CaptionBar.qml's CaptionButton, and
    // footerComponent below is in this same file, so S9's view-mode toggles can
    // reuse it without a new import.
    component ToolbarIconButton: ToolButton {
        // Without this, clicking one of these while the grid is showing (view
        // mode doesn't change, so no StackLayout focus handoff fires) leaves
        // focus on the button and arrow keys dead until the view is re-clicked.
        focusPolicy: Qt.NoFocus
        implicitWidth: 32
        implicitHeight: 32
        font.family: Theme.font.iconFamily
        ToolTip.delay: 500
        ToolTip.visible: hovered
    }

    // Logged-in chrome (header/footer/central StackLayout) only exists while
    // authController.authState === LoggedIn; otherwise the window shows just
    // LoginView, no header/footer. Header/footer are Loader-driven (Loader
    // can stand in directly for ApplicationWindow's header:/footer: slot
    // items); the central area is a third Loader switching between the
    // logged-in StackLayout and LoginView. This codebase's first Loader use
    // -- an exclusive two-state switch, not a multi-step screen flow, so
    // Loader rather than StackView.
    // The caption row is deliberately *outside* the authState gate below:
    // once the window is frameless, it is the only draggable area, so gating
    // it would leave LoginView with no way to move the window.
    header: ColumnLayout {
        spacing: 0

        CaptionBar {
            id: captionBar
            Layout.fillWidth: true
            windowAgent: winAgent
        }

        Loader {
            Layout.fillWidth: true
            active: authController.authState === AuthController.LoggedIn
            sourceComponent: headerComponent
        }
    }

    footer: Loader {
        active: authController.authState === AuthController.LoggedIn
        sourceComponent: footerComponent
    }

    Component {
        id: headerComponent

        // Address bar/breadcrumb only. Phase 9 wrapped this ToolBar in a
        // ColumnLayout to stack a TabStrip above it; Phase 17b moved that
        // strip onto the caption row itself (see CaptionBar.qml), leaving the
        // wrapper with a single child and no reason to exist.
        ToolBar {
            // Matches the caption row above it so the two read as one band
            // (S7-c); the default would be whatever the tallest child asks for.
            implicitHeight: Theme.rowHeight.bar

            // The active tab's rounded background ends flush against the top of
            // this row and has to continue into it, so the two must be the same
            // surface. Fluent's default ToolBar background happens to match in
            // dark, but is the panel shade in light, which would show the seam.
            background: Rectangle {
                color: Theme.color.surface

                // Carries the whole boundary on its own in light, where this
                // row and the content below it are both `surface`.
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: Theme.border.thin
                    color: Theme.color.stroke
                }
            }

            RowLayout {
                anchors.fill: parent
                // Horizontal only: the children are 32px inside a 40px row and
                // centre themselves in what's left, so a vertical margin here
                // would just make them overflow the layout it created.
                anchors.leftMargin: Theme.spacing.md
                anchors.rightMargin: Theme.spacing.md
                spacing: Theme.spacing.sm

                ToolbarIconButton {
                    text: Theme.glyph.back
                    ToolTip.text: qsTr("Back")
                    // tabsController.currentNavigation is only null
                    // during the brief login/logout state transition
                    // (see AuthController.authState's Connections below)
                    // -- ?./?? guard against that window.
                    enabled: tabsController.currentNavigation?.canGoBack ?? false
                    onClicked: tabsController.currentNavigation?.goBack()
                }

                // Goes to the current folder's parent, and -- unlike a "forward"
                // button -- pushes that onto the back stack like any other
                // navigation, so "back" undoes it (S7-a).
                ToolbarIconButton {
                    text: Theme.glyph.up
                    ToolTip.text: qsTr("Up")
                    enabled: tabsController.currentNavigation?.canGoUp ?? false
                    onClicked: tabsController.currentNavigation?.goUp()
                }

                // 700:300 against the search field below. Qt Quick Layouts
                // distributes space between fillWidth items in the ratio of
                // their preferred sizes ("If there are multiple items with
                // fillWidth set to true, the layout will grow or shrink the
                // items relative to the ratio of their preferred size" --
                // Qt 6.11 Layout docs), so the literals below are that ratio,
                // not pixel values. They were a bare 7 and 3 until S7 gave the
                // search field a minimumWidth: a preferred size below an item's
                // own minimum is silently raised to it, which turned the ratio
                // into 7:160 and left the breadcrumb three characters wide.
                // Both numbers therefore have to stay clear of that minimum.
                // minimumWidth: 0 is spelled out (it's already the default for
                // a non-layout item) because "no minimum width" is a deliberate
                // requirement here.
                Breadcrumb {
                    navController: tabsController.currentNavigation
                    dragProxy: moveDragProxy
                    model: tabsController.currentNavigation?.breadcrumb ?? []
                    Layout.fillWidth: true
                    Layout.preferredWidth: 700
                    Layout.minimumWidth: 0
                    Layout.fillHeight: true
                }

                // Qt 6.10's own search control, so the magnifier and the clear
                // button come from the style rather than from us (S7-b). It has
                // no placeholderText, which is the price of that.
                SearchField {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 300
                    // Unlike the breadcrumb beside it, this must not collapse to
                    // nothing: below roughly this the two indicators leave no
                    // room to type in.
                    Layout.minimumWidth: 160
                    // MegaApi::search() blocks the GUI thread synchronously, so
                    // search on Enter only rather than on every keystroke --
                    // live: true would freeze the UI once per keystroke.
                    live: false
                    // Enter (searchTriggered) and the magnifier
                    // (searchButtonPressed) are separate signals; only the first
                    // is gated by live.
                    onSearchTriggered: tabsController.currentNavigation?.search(text)
                    onSearchButtonPressed: tabsController.currentNavigation?.search(text)
                    // The style draws this button but doesn't empty the field,
                    // so clearing the text is ours to do as well.
                    onClearButtonPressed: {
                        text = "";
                        tabsController.currentNavigation?.search("");
                    }
                }

                ToolbarIconButton {
                    text: Theme.glyph.more
                    ToolTip.text: qsTr("More")
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
            // Chrome, like the caption row and the side panel -- not part of
            // the file-list surface (D3 leaves the footer's side unstated).
            background: Rectangle {
                color: Theme.color.surfaceAlt

                Rectangle {
                    anchors.top: parent.top
                    width: parent.width
                    height: Theme.border.thin
                    color: Theme.color.stroke
                }
            }

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

                // Uploads run on their own serial queue alongside downloads, so
                // both pairs can be showing at once. "n remaining" is the only
                // progress cue a serial queue offers beyond the active file.
                Label {
                    Layout.fillWidth: true
                    visible: uploadController.uploadActive
                    elide: Text.ElideMiddle
                    text: uploadController.pendingCount > 1 ? qsTr("↑ %1 (%2 remaining)").arg(
                                                                  uploadController.activeFileName).arg(
                                                                  uploadController.pendingCount
                                                                  - 1) : qsTr("↑ %1").arg(
                                                                  uploadController.activeFileName)
                }
                ProgressBar {
                    Layout.preferredWidth: 160
                    visible: uploadController.uploadActive
                    from: 0
                    to: 1
                    value: uploadController.activeProgress
                }

                // Keeps the view-mode buttons right-aligned when the transfer
                // groups above are hidden (RowLayout excludes invisible items).
                Item {
                    Layout.fillWidth: true
                    visible: !downloadController.downloadActive && !uploadController.uploadActive
                }

                // Reads/writes window.currentPane (the active tab's pane),
                // not window.viewMode directly -- each tab has its own
                // independent view mode since Phase 9; window.viewMode below
                // is only the last-write-wins *persisted default*, updated
                // via TabContentPane's viewModeWriteBack (see
                // mainContentComponent below), not the on-screen state of
                // any particular tab.
                ToolButton {
                    text: "☰"
                    checkable: true
                    checked: (window.currentPane?.viewMode ?? 0) === 0
                    // Clicking ⊞->☰ while already on the grid changes
                    // viewMode, which does trigger the StackLayout focus
                    // handoff -- but the symmetric case (clicking ☰ while
                    // already on the list) doesn't, so both buttons need
                    // this for consistency.
                    focusPolicy: Qt.NoFocus
                    onClicked: if (window.currentPane)
                                   window.currentPane.viewMode = 0
                }
                ToolButton {
                    text: "⊞"
                    checkable: true
                    checked: (window.currentPane?.viewMode ?? 0) === 1
                    focusPolicy: Qt.NoFocus
                    onClicked: if (window.currentPane)
                                   window.currentPane.viewMode = 1
                }
            }
        }
    }

    Dialog {
        id: signOutConfirmDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        standardButtons: Dialog.Yes | Dialog.Cancel
        // Neither DownloadService nor UploadService has a cancel API yet, so an
        // in-flight transfer is simply aborted by logout(). Warn about it up
        // front rather than silently dropping it.
        title: (downloadController.downloadActive || uploadController.uploadActive) ? qsTr(
                                                                                          "Sign out? (transfer in progress)") :
                                                                                      qsTr("Sign out?")
        onAccepted: authController.logout()
    }

    // Raised when a quick-access pin turns out to point at a folder that no
    // longer exists -- only reachable for a folder deleted *during* this
    // session (e.g. on another device), since the login-time sweep in
    // QuickAccessModel::reload silently drops the ones already gone.
    // Declining leaves the pin in place, so clicking it again asks again.
    Dialog {
        id: missingPinDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        standardButtons: Dialog.Yes | Dialog.Cancel

        property var pinHandle: 0
        property string pinName: ""

        title: qsTr("Folder no longer exists")
        Label {
            text: qsTr("\"%1\" could not be found. Remove it from Quick access?").arg(
                      missingPinDialog.pinName)
        }

        onAccepted: quickAccessModel.unpin(missingPinDialog.pinHandle)
    }

    // Phase 14b's two upload confirmations. Both live here rather than in
    // qml/components/ because all five drop targets reach them through a
    // single uploadController signal -- they're window-global singletons with
    // no reuse, unlike ConfirmRubbishDialog.qml which is instantiated per view.
    //
    // The destination rides along on each dialog instead of being remembered
    // in C++, so it stays alive exactly as long as the question does (same
    // shape as missingPinDialog above). destinationHandle is `property var`
    // because a quint64 doesn't survive QML's int/real property types.

    Dialog {
        id: folderDropDialog
        anchors.centerIn: Overlay.overlay
        modal: true
        standardButtons: Dialog.Yes | Dialog.Cancel

        property var filePaths: []
        property int folderCount: 0
        property var destinationHandle: 0
        property bool destinationIsRoot: false

        title: qsTr("Folders can't be uploaded")
        Label {
            text: qsTr("%1 folder(s) will be skipped. Upload the remaining %2 file(s)?").arg(
                      folderDropDialog.folderCount).arg(folderDropDialog.filePaths.length)
        }

        // Cancel needs no handler: nothing has been enqueued yet.
        onAccepted: uploadController.uploadFiles(folderDropDialog.filePaths,
                                                 folderDropDialog.destinationHandle,
                                                 folderDropDialog.destinationIsRoot)
    }

    // Three-way, so standardButtons can't express it -- a hand-built
    // DialogButtonBox with two ActionRole buttons and a RejectRole one. The
    // answers call uploadController directly rather than going through
    // onAccepted/onRejected.
    Dialog {
        id: nameConflictDialog
        anchors.centerIn: Overlay.overlay
        modal: true

        property var filePaths: []
        property var conflictNames: []
        property var destinationHandle: 0
        property bool destinationIsRoot: false

        title: qsTr("Files with the same name already exist")
        Label {
            width: 360
            wrapMode: Text.Wrap
            text: qsTr("%1 file(s) with the same name already exist in the destination:").arg(
                      nameConflictDialog.conflictNames.length) + "\n"
                  + nameConflictDialog.conflictNames.slice(0, 5).join(", ") + (
                      nameConflictDialog.conflictNames.length > 5 ? " …" : "")
        }

        footer: DialogButtonBox {
            Button {
                text: qsTr("Replace")
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: {
                    uploadController.uploadReplacingExisting(nameConflictDialog.filePaths,
                                                             nameConflictDialog.destinationHandle,
                                                             nameConflictDialog.destinationIsRoot);
                    nameConflictDialog.close();
                }
            }
            Button {
                text: qsTr("Skip")
                DialogButtonBox.buttonRole: DialogButtonBox.ActionRole
                onClicked: {
                    uploadController.uploadSkippingExisting(nameConflictDialog.filePaths,
                                                            nameConflictDialog.destinationHandle,
                                                            nameConflictDialog.destinationIsRoot);
                    nameConflictDialog.close();
                }
            }
            Button {
                text: qsTr("Cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
                onClicked: nameConflictDialog.close()
            }
        }
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

        SplitView {
            id: splitView
            anchors.fill: parent

            // Fluent's stock handle is a 2px fill that differs from the
            // surrounding surface by 4/255 -- invisible, and no wider than the
            // line it draws. Split the two concerns instead: a transparent
            // grab strip wide enough to hit, with a 1px rule down its middle.
            handle: Rectangle {
                id: splitHandle
                implicitWidth: 6
                // Reads as an extension of the panel rather than a neutral gap,
                // so the rule below lands exactly on the surface boundary. A
                // transparent handle puts the window surface on both sides of
                // the rule and the line looks 2px adrift into the content.
                color: Theme.color.surfaceAlt

                // SplitHandle attaches to the handle item itself; a child
                // reading it would get its own (never-set) attachment.
                readonly property bool active: SplitHandle.hovered || SplitHandle.pressed

                // A custom delegate loses the style's resize cursor.
                HoverHandler {
                    cursorShape: Qt.SplitHCursor
                }

                Rectangle {
                    anchors.right: parent.right
                    height: parent.height
                    width: splitHandle.active ? Theme.border.drop : Theme.border.thin
                    color: splitHandle.active ? Theme.color.accent : Theme.color.stroke
                }
            }

            SidePanel {
                id: treePanel
                navController: tabsController.currentNavigation
                dragProxy: moveDragProxy
                SplitView.minimumWidth: 120
                SplitView.maximumWidth: 500

                // One-shot imperative read of the persisted width, not a
                // live binding: SplitView itself writes back to
                // treePanel.width as the user drags the splitter (see
                // onResizingChanged below), and a live binding here would
                // fight that write back and forth -- same "read once at
                // creation" convention as TabContentPane.qml's
                // initialViewMode.
                Component.onCompleted: treePanel.SplitView.preferredWidth = window.treePanelWidth
            }

            StackLayout {
                SplitView.fillWidth: true
                currentIndex: tabsController.currentIndex

                Repeater {
                    id: paneRepeater
                    model: tabsController

                    // navigation/thumbnails below come from
                    // TabsController::roleNames() ("navigation"/"thumbnails",
                    // see TabsController.h's Roles enum) -- required properties
                    // on a Repeater delegate are populated straight from the
                    // model's role data for a QAbstractItemModel-backed model.
                    TabContentPane {
                        id: pane
                        required property var navigation
                        required property var thumbnails
                        navController: navigation
                        thumbController: thumbnails
                        dragProxy: moveDragProxy

                        // Read once at this tab's creation (see
                        // TabContentPane.qml's own comment on why these are
                        // required properties rather than live bindings) --
                        // window.* is this file's single Settings-backed,
                        // last-write-wins copy.
                        initialViewMode: window.viewMode
                        initialSortColumn: window.sortColumn
                        initialSortAscending: window.sortAscending
                        initialColumnWidthName: window.columnWidthName
                        initialColumnWidthModified: window.columnWidthModified
                        initialColumnWidthSize: window.columnWidthSize

                        onViewModeWriteBack: vm => window.viewMode = vm
                        onSortOrderWriteBack: (column, ascending) => {
                            window.sortColumn = column;
                            window.sortAscending = ascending;
                        }
                        onColumnWidthsWriteBack: (nameWidth, modifiedWidth, sizeWidth) => {
                            window.columnWidthName = nameWidth;
                            window.columnWidthModified = modifiedWidth;
                            window.columnWidthSize = sizeWidth;
                        }

                        // StackLayout keeps every pane alive and just toggles
                        // visible, and an invisible item can't hold activeFocus
                        // -- so focus has to be handed back explicitly on every
                        // tab switch, or arrow keys go dead until the view is
                        // re-clicked.
                        StackLayout.onIsCurrentItemChanged: if (StackLayout.isCurrentItem)
                                                                Qt.callLater(()
                                                                             => pane.focusActiveView(
                                                                                    ))
                    }
                }

                // Keeps window.currentPane pointing at whichever pane is
                // actually showing, for footerComponent above (a sibling nested
                // Component -- it can't see paneRepeater by id directly, only
                // window's own properties). Re-evaluates whenever
                // tabsController.currentIndex or paneRepeater.count changes
                // (both genuine notifying properties read inside the
                // expression); itemAt() itself is just a plain method call, so
                // it's read fresh on every such re-evaluation rather than cached.
                Binding {
                    target: window
                    property: "currentPane"
                    value: paneRepeater.count > 0 ? paneRepeater.itemAt(
                                                        tabsController.currentIndex) : null
                }
            }

            onResizingChanged: if (!resizing)
                                   window.treePanelWidth = treePanel.width
        }
    }

    // Phase 14a's move drag & drop. One instance for the whole window, parented
    // to the Overlay so it escapes the file views' Flickable clipping on its way
    // to the side panel -- see DragProxy.qml's own comment. Handed down as a
    // required property (the same route navController takes) because a
    // separately-loaded .qml file can't reach Main.qml by id.
    // id deliberately differs from the `dragProxy` property name it gets
    // assigned to below: inside an object that declares its own `dragProxy`,
    // that property shadows a same-named outer id, so `dragProxy: dragProxy`
    // would silently bind the property to itself and evaluate to undefined.
    DragProxy {
        id: moveDragProxy
        parent: Overlay.overlay
    }

    DownloadSnackbar {
        id: downloadSnackbar
        parent: Overlay.overlay
    }

    ErrorToast {
        id: errorToast
        parent: Overlay.overlay
    }

    OperationSnackbar {
        id: operationSnackbar
        parent: Overlay.overlay
    }

    Connections {
        target: downloadController
        function onDownloadFinished(success, fileName, localPath, errorMessage, alreadyPresent) {
            downloadSnackbar.show(success, fileName, localPath, errorMessage, alreadyPresent);
        }
    }

    // The two-dialog chain needs no explicit state machine: answering the
    // folder question calls uploadFiles(), which raises the name-conflict one
    // by itself if it finds collisions.
    Connections {
        target: uploadController
        function onFolderDropRequiresConfirmation(filePaths, folderCount, destinationHandle,
                                                  destinationIsRoot) {
            folderDropDialog.filePaths = filePaths;
            folderDropDialog.folderCount = folderCount;
            folderDropDialog.destinationHandle = destinationHandle;
            folderDropDialog.destinationIsRoot = destinationIsRoot;
            folderDropDialog.open();
        }
        function onNameConflictRequiresConfirmation(filePaths, conflictNames, destinationHandle,
                                                    destinationIsRoot) {
            nameConflictDialog.filePaths = filePaths;
            nameConflictDialog.conflictNames = conflictNames;
            nameConflictDialog.destinationHandle = destinationHandle;
            nameConflictDialog.destinationIsRoot = destinationIsRoot;
            nameConflictDialog.open();
        }
    }

    Connections {
        target: notificationController
        function onErrorOccurred(context, errorMessage) {
            errorToast.show(context, errorMessage);
        }
        function onOperationFinished(context, succeeded, failed) {
            operationSnackbar.show(context, succeeded, failed);
        }
    }

    Connections {
        target: authController
        function onAuthStateChanged() {
            if (authController.authState === AuthController.LoggedIn) {
                tabsController.loadRootAll();
                folderTreeModel.reload();
                quickAccessModel.reload();
            } else if (authController.authState === AuthController.LoggedOut) {
                tabsController.resetAll();
                folderTreeModel.reset();
                quickAccessModel.reset();
            }
        }
    }

    // QuickAccessModel verifies a pin's target before anything happens, then
    // reports back here -- it deliberately knows nothing about tabs or dialogs.
    Connections {
        target: quickAccessModel
        function onActivated(handle, inNewTab) {
            if (inNewTab)
                tabsController.addTabAt(handle, false);
            else
                tabsController.currentNavigation?.navigateTo(handle, false);
        }
        function onMissing(handle, name) {
            missingPinDialog.pinHandle = handle;
            missingPinDialog.pinName = name;
            missingPinDialog.open();
        }
    }

    Connections {
        target: tabsController
        function onLastTabClosed() {
            window.close();
        }
    }
}
