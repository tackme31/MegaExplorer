import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls").
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts
import QtCore
// Directory import for ToastStack.qml/TabStrip.qml -- the CMake-generated
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

        if (authController.authState === AuthController.LoggedIn)
            window.loadSignedInContent();
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

    // Phase 15's preview pane. The width is window-wide like treePanelWidth
    // above, but previewVisible is the *new tab's* starting point in the
    // last-write-wins sense viewMode is -- whether the pane shows is per-tab
    // (TabContentPane.qml's previewVisible), while how wide it is cannot be:
    // SplitView.preferredWidth attaches to the one pane item, so N tabs would
    // need N panes, N controllers and N temp files to disagree about it.
    property bool previewVisible: false
    property real previewPaneWidth: 320

    // The theme the user picked in the settings dialog, as a Qt::ColorScheme
    // value (Qt.Unknown = follow the OS). Persisted below like the rest, but
    // deliberately *not* applied from here on startup: main.cpp already read
    // this same key and applied it before the first frame, and doing it again
    // once Settings restores the alias would overrun the
    // MEGAEXPLORER_COLOR_SCHEME override that only main.cpp can see.
    property int colorSchemePreference: Qt.Unknown

    // The local folder that stands in for the MEGA root, as a native path; empty
    // means nothing is linked. Persisted here like the theme, and pushed into
    // localFolderController below -- C++ owns no copy of its own, so there is one
    // place this value can come from.
    property string localRootFolder: ""

    // What the video and audio viewers play at, on the same 0..1 scale their
    // slider shows. Owned here rather than per viewer for viewMode's reason: a
    // viewer window is created and destroyed per double-click, several stand at
    // once, and N Settings items would fight over the one pair of keys.
    property real playbackVolume: 1.0
    property bool playbackMuted: false

    // The currently active tab's TabContentPane, kept in sync by the Binding
    // inside mainContentComponent below and injected into StatusBar.qml, which
    // reads/writes it to drive the view-mode toggle against whichever tab is
    // actually showing. It exists as a window property because neither consumer
    // can reach mainContentComponent's internal ids: the footer is a separately
    // loaded file, and even before R6-3 it was a sibling nested Component.
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
        property alias previewVisible: window.previewVisible
        property alias previewPaneWidth: window.previewPaneWidth
        property alias colorSchemePreference: window.colorSchemePreference
        property alias localRootFolder: window.localRootFolder
        property alias playbackVolume: window.playbackVolume
        property alias playbackMuted: window.playbackMuted
    }

    // Pushed rather than read: LocalFolderController keeps no persisted copy, so
    // this is what carries the restored Settings value into C++ at startup and
    // every change after it.
    Binding {
        target: localFolderController
        property: "localRoot"
        value: window.localRootFolder
    }

    // A window-level Shortcut is fine here, unlike FileTableView's Ctrl+A:
    // neither F5 nor Ctrl+R means anything to a focused text field, so ignoring
    // focus costs nothing. `sequences`, not `sequence`: StandardKey.Refresh is
    // both of those on Windows and the singular form silently binds only F5.
    Shortcut {
        sequences: [StandardKey.Refresh]
        onActivated: tabsController.currentNavigation?.refresh()
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
            dragProxy: moveDragProxy
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

        AddressToolBar {
            dragProxy: moveDragProxy
            onTransfersRequested: transferFlyout.show()
            onAboutRequested: aboutDialog.open()
            onSettingsRequested: settingsDialog.open()
            onLicensesRequested: licenseDialog.open()
            onSignOutRequested: signOutDialog.open()
        }
    }

    Component {
        id: footerComponent

        StatusBar {
            currentPane: window.currentPane
        }
    }

    // One instance each for the whole app -- nothing about them is per-tab. The
    // last three wire themselves to their controller's signal, so nothing here
    // opens them.
    SignOutDialog {
        id: signOutDialog
        auth: authController
        downloads: downloadController
        uploads: uploadController
    }

    AboutDialog {
        id: aboutDialog
        onLicensesRequested: licenseDialog.open()
    }

    LicenseDialog {
        id: licenseDialog
    }

    SettingsDialog {
        id: settingsDialog
        colorScheme: window.colorSchemePreference
        // Applied here and not inside the dialog: styleHints is app-wide state,
        // and this is the only window. Assigning Qt.Unknown is how QStyleHints
        // is told to follow the OS again.
        onColorSchemeSelected: scheme => {
            window.colorSchemePreference = scheme;
            Application.styleHints.colorScheme = scheme;
        }
        localRootFolder: window.localRootFolder
        onLocalRootFolderSelected: path => window.localRootFolder = path
    }

    MissingPinDialog {
        quickAccess: quickAccessModel
    }

    PropertiesDialog {
        properties: propertiesController
    }

    ConfirmUploadDialog {
        uploads: uploadController
    }

    NameConflictDialog {
        uploads: uploadController
        // Injected rather than read inside the dialog, same as CopyConflictDialog:
        // that keeps the file free of root-context lookups and so testable by
        // tst_MainDialogs.qml.
        fileVersioningEnabled: accountController.fileVersioningEnabled
    }

    Loader {
        anchors.fill: parent
        sourceComponent: authController.authState === AuthController.LoggedIn
                         ? mainContentComponent : loginComponent
    }

    // One viewer window per double-click, so several stand side by side and each is
    // dismissed on its own.
    Component {
        id: imageViewerComponent

        ImageViewer {
            id: imageViewerWindow
            controller: viewerController
            // Set here rather than inherited from nesting, which is what supplied it
            // while the viewer was declared inside this window: a transient window
            // floats above its parent, minimises and closes with it, and does not
            // count towards quitOnLastWindowClosed -- so a viewer left open cannot
            // keep the app alive after the main window goes.
            transientParent: window
            onVisibleChanged: if (!imageViewerWindow.visible)
                                  imageViewerWindow.destroy()
        }
    }

    Component {
        id: videoViewerComponent

        VideoViewer {
            id: videoViewerWindow
            controller: viewerController
            transientParent: window
            // Handed down as a binding and changed only by asking back up, so a
            // second viewer's transport bar follows this one instead of the two
            // drifting apart -- assigning to the property here would break it.
            volume: window.playbackVolume
            muted: window.playbackMuted
            onVolumeRequested: level => window.playbackVolume = level
            onMuteToggleRequested: window.playbackMuted = !window.playbackMuted
            onVisibleChanged: if (!videoViewerWindow.visible)
                                  videoViewerWindow.destroy()
        }
    }

    Component {
        id: audioViewerComponent

        AudioViewer {
            id: audioViewerWindow
            controller: viewerController
            transientParent: window
            volume: window.playbackVolume
            muted: window.playbackMuted
            onVolumeRequested: level => window.playbackVolume = level
            onMuteToggleRequested: window.playbackMuted = !window.playbackMuted
            onVisibleChanged: if (!audioViewerWindow.visible)
                                  audioViewerWindow.destroy()
        }
    }

    Component {
        id: pdfViewerComponent

        PdfViewer {
            id: pdfViewerWindow
            controller: viewerController
            transientParent: window
            onVisibleChanged: if (!pdfViewerWindow.visible)
                                  pdfViewerWindow.destroy()
        }
    }

    function openViewer(handle, name): void {
        const kind = viewerController.viewerKind(name);
        const component = kind === "image" ? imageViewerComponent : kind === "video" ? videoViewerComponent : kind === "pdf" ? pdfViewerComponent : kind === "audio" ? audioViewerComponent : null;
        if (!component)
            return;
        const viewer = component.createObject(window);
        if (!viewer)
            return;
        viewer.open(handle, name);
        // A viewer that never became visible would never reach the onVisibleChanged
        // that destroys it.
        if (!viewer.showing)
            viewer.destroy();
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

                // Which edge the rule sits on depends on which side of this
                // handle the surfaceAlt panel is: left for the tree panel,
                // right for the preview pane. One delegate serves every
                // handle, so it has to work this out rather than be told.
                readonly property bool panelOnLeft: splitHandle.x < contentStack.x

                Rectangle {
                    anchors.right: splitHandle.panelOnLeft ? parent.right : undefined
                    anchors.left: splitHandle.panelOnLeft ? undefined : parent.left
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
                id: contentStack
                // Stated, not left to SplitView's "last visible child fills"
                // default: adding the preview pane below would otherwise hand
                // the fill to it.
                SplitView.fillWidth: true
                currentIndex: tabsController.currentIndex

                Repeater {
                    id: paneRepeater
                    model: tabsController

                    // navigation/mutations/thumbnails below come from
                    // TabsController::roleNames() ("navigation"/"mutations"/
                    // "thumbnails", see TabsController.h's Roles enum) --
                    // required properties on a Repeater delegate are populated
                    // straight from the model's role data for a
                    // QAbstractItemModel-backed model.
                    TabContentPane {
                        id: pane
                        required property var navigation
                        required property var mutations
                        required property var thumbnails
                        navController: navigation
                        mutController: mutations
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
                        initialPreviewVisible: window.previewVisible

                        onFileActivated: (handle, name) => window.openViewer(handle, name)

                        onViewModeWriteBack: vm => window.viewMode = vm
                        onPreviewVisibleWriteBack: v => window.previewVisible = v
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
                // actually showing -- see that property's own comment for why
                // the status bar has to be reached through it rather than
                // seeing paneRepeater directly. Re-evaluates whenever
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

            PreviewPane {
                id: previewPane
                controller: previewController
                currentPane: window.currentPane
                visible: window.currentPane?.previewVisible ?? false
                SplitView.minimumWidth: 220
                SplitView.maximumWidth: 800

                // Re-applied on every show, not read once like treePanel's
                // width above: SplitView drops a hidden child out of its
                // layout entirely, and the tab switches that drive `visible`
                // here happen throughout the session rather than at creation.
                // Idempotent, since onResizingChanged writes any user drag
                // straight back to window.previewPaneWidth.
                onVisibleChanged: if (visible)
                                      previewPane.SplitView.preferredWidth = window.previewPaneWidth
            }

            onResizingChanged: if (!resizing) {
                                   window.treePanelWidth = treePanel.width;
                                   if (previewPane.visible)
                                       window.previewPaneWidth = previewPane.width;
                               }
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

    // One stack for all three kinds of notification (S10). Deliberately NOT
    // parented to Overlay.overlay the way the three Popups it replaces were:
    // the overlay spans the whole window, so a bottom-pinned stack rides on top
    // of the status bar. Left in contentItem it lands between header and
    // footer, and being declared after the content Loader above puts it over
    // the file views. Modal dialogs still cover it -- they are on the overlay.
    ToastStack {
        id: toastStack
        maxFilesPerUpload: uploadController.maxFilesPerUpload
        // The flyout below owns the bottom-right corner while it is up; the
        // toasts sit above it rather than computing the corner a second time.
        bottomInset: transferFlyout.reservedHeight
    }

    // Same parenting argument as the stack above, and declared after it so it
    // takes the corner the stack is now offset from.
    TransferFlyout {
        id: transferFlyout
        transfers: transferListModel
        // Both queues unconditionally: the panel merges them into one list, so
        // "Cancel all" cannot mean one direction. Either call is a no-op when its
        // own queue is empty, toast included.
        onCancelAllRequested: {
            downloadController.cancelDownloads();
            uploadController.cancelUploads();
        }
        // A single row, unlike the button above, names exactly one queue.
        onCancelRequested: (direction, jobId) => {
            if (direction === TransferDirection.Upload)
                uploadController.cancelJob(jobId);
            else
                downloadController.cancelJob(jobId);
        }
        // The same call the download toast's "Open" makes.
        onOpenRequested: path => downloadController.openFile(path)
    }

    Connections {
        target: downloadController
        function onDownloadFinished(success, fileName, localPath) {
            toastStack.showDownload(success, fileName, localPath);
        }
    }

    Connections {
        target: notificationController
        function onErrorOccurred(context, reason, rawMessage) {
            toastStack.showError(context, reason, rawMessage);
        }
        function onOperationFinished(context, succeeded, failed, undo) {
            toastStack.showOperation(context, succeeded, failed, undo);
        }
    }

    // Guard, not a live path: main.cpp starts the session restore before this file is
    // built, and only the queued GUI-thread hop keeps LoggedIn from landing before the
    // handler below exists -- which a signal handler could never see. The flag is what
    // stops the two entry points loading the tree twice.
    property bool signedInContentLoaded: false

    function loadSignedInContent(): void {
    if (window.signedInContentLoaded)
    return;
    window.signedInContentLoaded = true;
    tabsController.loadRootAll();
    folderTreeModel.reload();
    quickAccessModel.reload();
}

    Connections {
        target: authController
        function onAuthStateChanged() {
            if (authController.authState === AuthController.LoggedIn) {
                window.loadSignedInContent();
            } else if (authController.authState === AuthController.LoggedOut) {
                window.signedInContentLoaded = false;
                tabsController.resetAll();
                folderTreeModel.reset();
                quickAccessModel.reset();
                // Node handles belong to the account that was signed in, so
                // they can't survive into the next session.
                clipboardController.clear();
                // Same reasoning for the profile and storage figures. There is
                // deliberately no matching call in the LoggedIn branch above:
                // account data is loaded lazily, when the menu first opens.
                accountController.reset();
            }
        }
    }

    // QuickAccessModel verifies a pin's target before anything happens, then
    // reports back -- it deliberately knows nothing about tabs. Its other
    // signal, missing(), is MissingPinDialog.qml's to handle.
    Connections {
        target: quickAccessModel
        function onActivated(handle, inNewTab) {
            if (inNewTab)
                tabsController.addTabAt(handle, false);
            else
                tabsController.currentNavigation?.navigateTo(handle, false);
        }
    }

    Connections {
        target: tabsController
        function onLastTabClosed() {
            window.close();
        }
    }
}
