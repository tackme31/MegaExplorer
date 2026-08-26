import QtQuick
import QtQuick.Layouts

// One tab's content: a StackLayout switching between the Explorer-style
// detail list (FileTableView) and the thumbnail grid (FileGridView), plus
// this tab's own view-mode/sort-order/column-width state. Instantiated once
// per tab by Main.qml's Repeater over tabsController and kept alive for the
// tab's whole lifetime -- Repeater + StackLayout keep every pane's Item tree
// around, only toggling visibility, so scroll position/selection/focus
// survive a tab switch (the entire point of Phase 9's per-tab panes).
ColumnLayout {
    id: pane
    spacing: 0

    required property var navController
    // This tab's FileMutationController. Not named "mutations" to match the
    // model role it comes from: a property of that name would shadow the outer
    // role inside Main.qml's delegate and self-bind to undefined -- the same
    // trap Main.qml documents for dragProxy.
    required property var mutController
    required property var thumbController
    // Main.qml's single window-wide DragProxy, passed through to both views --
    // see Main.qml's own comment on why this is drilled down rather than
    // reached by id.
    required property var dragProxy

    // Read once (Component.onCompleted below), not bound live: Main.qml
    // passes in the single Settings-backed window.* value at the moment this
    // tab is created ("new tab's initial value" per the last-write-wins
    // spec), and this tab's own viewMode/sortColumn/etc. below stay fixed at
    // that starting point afterward -- a live binding here would instead
    // make every already-open tab keep tracking whichever tab wrote last,
    // which is exactly what last-write-wins is not supposed to do (only
    // *new* tabs pick up the latest value). Main.qml can't be referenced by
    // id from this separately-loaded file (see FileTableView.qml's own
    // comment on the same constraint), hence passing these in explicitly
    // rather than reading window.* directly.
    required property int initialViewMode
    required property int initialSortColumn
    required property bool initialSortAscending
    required property real initialColumnWidthName
    required property real initialColumnWidthModified
    required property real initialColumnWidthSize
    required property bool initialPreviewVisible

    // 0 = list, 1 = grid. Literal default (not bound to initialViewMode) --
    // see the required-property block's comment above for why the real
    // starting value is assigned once, imperatively, in Component.onCompleted.
    property int viewMode: 0

    // Whether the window's single preview pane shows while this tab is the
    // active one. Per-tab for the same reason viewMode is, and so that the
    // status bar's three toggles all read the same object.
    property bool previewVisible: false

    // Relayed up to Main.qml, which writes it through to the single Settings
    // instance -- see this file's top comment. Also fires once during
    // Component.onCompleted below, an idempotent echo of the value this tab
    // just read.
    signal viewModeWriteBack(int viewMode)
    onViewModeChanged: pane.viewModeWriteBack(pane.viewMode)

    signal previewVisibleWriteBack(bool previewVisible)
    onPreviewVisibleChanged: pane.previewVisibleWriteBack(pane.previewVisible)

    Component.onCompleted: {
        pane.viewMode = pane.initialViewMode;
        pane.previewVisible = pane.initialPreviewVisible;
    }

    // Called by Main.qml's Repeater delegate (StackLayout.onIsCurrentItemChanged)
    // when this pane's tab becomes the active one -- hands focus to whichever
    // of the table/grid views is currently showing, since an invisible item
    // (StackLayout keeps both alive, just toggles visible) can't hold
    // activeFocus.
    function focusActiveView() {
        Qt.callLater(() => {
            if (stackLayout.currentIndex === 0)
                fileTableView.forceActiveFocus();
            else
                fileGridView.forceActiveFocus();
        });
    }

    // Single entry point both child views' activateRequested funnels into.
    // Files are inert on purpose -- double-click used to download and misfired
    // too easily, so name/sizeBytes go unused and downloading is context-menu only.
    function activate(isFolder, handle, name, sizeBytes) {
        if (isFolder)
            pane.navController.openFolder(handle);
    }

    // Plain Item, not the StackLayout itself: a StackLayout treats every Item
    // child as a page, so the empty-state notice below can only be laid over the
    // views from outside it.
    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true
        // Breathing room between the SplitView's rule and the file views, which
        // otherwise start flush against it (S8a). It lives here rather than on
        // Main.qml's SplitView because SplitView is not a QQuickLayout, so
        // Layout.leftMargin on its second child would be ignored.
        Layout.leftMargin: Theme.spacing.md

        StackLayout {
            id: stackLayout
            anchors.fill: parent
            currentIndex: pane.viewMode

            FileTableView {
                id: fileTableView
                navController: pane.navController
                mutController: pane.mutController
                dragProxy: pane.dragProxy
                initialSortColumn: pane.initialSortColumn
                initialSortAscending: pane.initialSortAscending
                initialColumnWidthName: pane.initialColumnWidthName
                initialColumnWidthModified: pane.initialColumnWidthModified
                initialColumnWidthSize: pane.initialColumnWidthSize
                // StackLayout keeps both children alive and just toggles
                // visible, and an invisible item can't hold activeFocus -- so
                // focus has to be handed back explicitly on every switch, or
                // arrow keys go dead until the view is re-clicked. Qt.callLater
                // defers past StackLayout's own visibility update, which runs
                // after this notification fires.
                StackLayout.onIsCurrentItemChanged: if (StackLayout.isCurrentItem)
                                                        Qt.callLater(()
                                                                     => fileTableView.forceActiveFocus(
                                                                            ))
                onActivateRequested: (isFolder, handle, name, sizeBytes) => pane.activate(isFolder,
                                                                                          handle, name,
                                                                                          sizeBytes)
                onOpenInNewTabRequested: handle => tabsController.addTabAt(handle, false)
                onNewFolderRequested: newFolderDialog.prompt()
                onSortOrderChanged: (column, ascending) => pane.sortOrderWriteBack(column,
                                                                                   ascending)
                onColumnWidthsChanged: (nameWidth, modifiedWidth, sizeWidth)
                                       => pane.columnWidthsWriteBack(nameWidth, modifiedWidth,
                                                                     sizeWidth)
            }

            FileGridView {
                id: fileGridView
                navController: pane.navController
                mutController: pane.mutController
                thumbController: pane.thumbController
                dragProxy: pane.dragProxy
                StackLayout.onIsCurrentItemChanged: if (StackLayout.isCurrentItem)
                                                        Qt.callLater(()
                                                                     => fileGridView.forceActiveFocus(
                                                                            ))
                onActivateRequested: (isFolder, handle, name, sizeBytes) => pane.activate(isFolder,
                                                                                          handle, name,
                                                                                          sizeBytes)
                onOpenInNewTabRequested: handle => tabsController.addTabAt(handle, false)
                onNewFolderRequested: newFolderDialog.prompt()
            }
        }

        // After the StackLayout, so it paints over the views rather than under
        // them.
        EmptyListingNotice {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: Theme.spacing.xl * 3
            width: parent.width - 2 * Theme.spacing.xl
            navController: pane.navController
        }

        // Same slot, and never on screen at the same time as the notice above --
        // see its own header for the split.
        ListingLoadingNotice {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: Theme.spacing.xl * 3
            navController: pane.navController
        }
    }

    // One per tab rather than per view: it reports through this tab's
    // mutController, which both views share, so a second instance would react
    // to the same signals. Dialog is a Popup, not an Item, so it isn't laid
    // out by this ColumnLayout.
    NewFolderDialog {
        id: newFolderDialog
        mutController: pane.mutController
    }

    // Same reasoning, one per tab: the question is raised by this tab's
    // mutController, whether the copy or move came from Ctrl+V, Ctrl+X or a drag.
    CopyConflictDialog {
        id: copyConflictDialog
        mutController: pane.mutController
        // Injected rather than read inside the dialog, which keeps that file free of
        // root-context lookups and so testable by tst_MainDialogs.qml.
        fileVersioningEnabled: accountController.fileVersioningEnabled
    }

    // The refusal, not a question: a set carrying one name twice never reaches
    // CopyConflictDialog. Same list widget so the names read the same way here.
    ConflictNameListDialog {
        id: duplicateNameDialog

        message: qsTr("Two or more of these have the same name, so they can't be copied "
                      + "or moved together. Leave one of each and try again.")
    }

    Connections {
        target: pane.mutController
        function onDuplicateNamesRejected(entries) {
            duplicateNameDialog.entries = entries;
            duplicateNameDialog.open();
        }
    }

    // uploadController is app-global (three of the five drop targets are shared
    // chrome with no owning tab), so it broadcasts the destination and each tab
    // decides for itself whether it's the one showing it. Connections is a
    // QtObject, so it isn't laid out by this ColumnLayout.
    Connections {
        target: uploadController
        function onDestinationChanged(handle, isRoot) {
            pane.navController.refreshIfShowing(handle, isRoot);
        }
    }

    // Relayed up to Main.qml alongside viewModeWriteBack above -- see
    // FileTableView.qml's own top comment for why sort order/column widths
    // funnel through here rather than each tab owning a Settings item.
    signal sortOrderWriteBack(int column, bool ascending)
    signal columnWidthsWriteBack(real nameWidth, real modifiedWidth, real sizeWidth)
}
