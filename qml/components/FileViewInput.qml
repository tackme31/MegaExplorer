import QtQuick

// The view-level input layer both file views share (extracted in R6-2b):
// keyboard, hover, background taps and the three popups those reach. What stays
// behind in a view is the geometry the injected callables below wrap.
//
// Every handler here is parented to `view`, the Flickable viewport. Declared
// inside a Flickable they would be installed on its contentItem instead (Qt
// docs, TableView::cellAtPosition), which is only as tall as the content -- so
// they would never see a tap below the last row. The cost is that positions
// arrive in viewport coordinates and rowAtPos has to map them.
//
// The left-button handler also does the row selection, not just the clearing:
// the default gesturePolicy (DragThreshold) takes a passive grab only, so a
// per-delegate TapHandler fires in addition to this one and Ctrl+click would
// toggle the same row twice, cancelling itself out.
//
// This root Item deliberately gets no `parent: view` + `anchors.fill` of its
// own, unlike FileViewDropArea: that would put it above the delegates in
// pointer delivery order, and a delegate DragHandler could no longer cancel
// this layer's pending tap when it passes the drag threshold -- which is what
// keeps a drag off an already-selected row from collapsing the selection.
Item {
    id: root

    // The viewport the handlers live on, and whose scrolling re-resolves hover.
    // Typed Flickable so FileTableView cannot pass its ColumnLayout root by
    // mistake -- the flickable there is a child.
    required property Flickable view

    required property var navController
    required property var mutController

    // The clipboardController context property. Not named clipboardController: a
    // property of that name would shadow the context property in this scope, so
    // the site's `clipboardController: clipboardController` would bind to itself
    // and land as undefined (same trap as FileViewDropArea's uploads).
    required property var clipboard

    // Hit test supplied by the host: a position in view coordinates -> row
    // index, -1 for "nothing there". Same injection as FileViewDropArea's.
    required property var rowAtPos

    // Moves active focus to the view for a mouse interaction.
    // view.forceActiveFocus() cannot stand in for it: GridView is a focus scope
    // and hands active focus straight back to the rename field, while
    // FileTableView's only focus target is its ColumnLayout root, not the
    // TableView passed as `view`. Mind the name at the grid site -- FileGridView
    // has a function called takeFocus too, so `takeFocus: () => root.takeFocus()`
    // self-recurses if the `root.` is dropped.
    required property var takeFocus

    // Scrolls a row into view. Its own injection point because the two views
    // disagree on method, enum and receiver alike: positionViewAtIndex(row,
    // GridView.Contain) against positionViewAtRow(row, TableView.Contain).
    required property var revealRow

    // Cursor geometry for the arrow keys -- see cursorDelta().
    required property int arrowColumns
    required property bool horizontalArrows

    // Handle of the row being renamed in place, 0 when not renaming (same
    // meaningless-sentinel convention as PathSegment::isRoot's handle). The
    // host's delegate reads it to swap its label for an InlineRenameField.
    property var renamingHandle: 0

    // Row under the pointer, resolved once here rather than per delegate: a
    // plain child of a Flickable rides contentItem, which scrolls away under a
    // stationary pointer, and in the table a per-cell handler cannot reach the
    // strip right of the last column, which still belongs to the row (S6/S6a).
    property int hoverRow: -1

    // Raised by the empty-space menu, relayed by the host to the tab's single
    // NewFolderDialog.
    signal newFolderRequested

    // Shared by F2 and the context menu's renameRequested. Renaming is
    // inherently single-item, so this collapses the selection to the cursor row
    // first -- selectRow(row, Qt.NoModifier) already means "deselect everything
    // else and select just this row". After a right-click the cursor is already
    // on the clicked row (the delegate's right-button TapHandler selects it), so
    // both entry points land here identically.
    function beginRename() {
        if (root.renamingHandle !== 0)
            return;
        const model = root.navController.fileListModel;
        const row = model.cursorRow();
        if (row < 0)
            return;
        model.selectRow(row, Qt.NoModifier);
        const entries = model.selectedEntries();
        if (entries.length !== 1)
            return;
        root.renamingHandle = entries[0].handle;
        root.revealRow(row);
    }

    // Focus must be handed back explicitly, same reason the focusPolicy:
    // Qt.NoFocus assignments elsewhere exist -- otherwise arrow keys go dead
    // once the field is gone.
    function endRename() {
        root.renamingHandle = 0;
        root.takeFocus();
    }

    // Called from the field's committed signal, so tearing the field down is
    // deferred past the end of that signal's own handler.
    function commitRename(handle, oldName, newName) {
        if (newName !== oldName)
            root.mutController.renameEntry(handle, newName);
        Qt.callLater(root.endRename);
    }

    // Shared by Ctrl+C/Ctrl+X and the context menu's cut/copy entries. The
    // source folder is recorded with the entries because the paste may well
    // happen in another tab, long after this one has navigated elsewhere.
    function putOnClipboard(cut) {
        const entries = root.navController.fileListModel.selectedEntries();
        if (entries.length === 0)
            return;
        if (cut)
            root.clipboard.cut(entries, root.navController.currentHandle,
                               root.navController.atRoot);
        else
            root.clipboard.copy(entries, root.navController.currentHandle,
                                root.navController.atRoot);
    }

    // undefined means "not a cursor key" -- the caller then leaves
    // event.accepted alone. Returning 0 instead would fall through to
    // moveCursor(0, ...) and swallow the key.
    //
    // A one-column view and a view without horizontal arrows are different
    // things: FileTableView leaves Left/Right unaccepted on purpose, and
    // generalizing on arrowColumns: 1 alone would turn Left into "one row up"
    // *and* consume the key. Hence the second, declarative property.
    function cursorDelta(key) {
        if (key === Qt.Key_Up)
            return -root.arrowColumns;
        if (key === Qt.Key_Down)
            return root.arrowColumns;
        if (!root.horizontalArrows)
            return undefined;
        if (key === Qt.Key_Left)
            return -1;
        if (key === Qt.Key_Right)
            return 1;
        return undefined;
    }

    // A body rather than an inline Keys.onPressed handler because the attached
    // property only fires on the item that holds activeFocus, which is the view
    // and never this child -- so the host keeps the one-line attachment and
    // hands the event here.
    function handleKey(event) {
        if (event.modifiers & Qt.AltModifier)
            return; // reserved for a future Alt+Left "back" shortcut

        // While the rename field has focus this is still on its key-propagation
        // path, and it doesn't consume F2/Delete the way it consumes arrows and
        // Ctrl+A -- so the view has to stand down explicitly.
        if (root.renamingHandle !== 0)
            return;

        if (event.key === Qt.Key_F2) {
            root.beginRename();
            event.accepted = true;
            return;
        }

        if (event.key === Qt.Key_Delete) {
            confirmRubbishDialog.confirm();
            event.accepted = true;
            return;
        }

        if (event.matches(StandardKey.SelectAll)) {
            root.navController.fileListModel.selectAll();
            event.accepted = true;
            return;
        }

        // Below the Delete branch above on purpose: StandardKey.Cut is Ctrl+X
        // *and* Shift+Delete on Windows, and that branch tests no modifiers, so
        // moving these up would silently turn Shift+Delete from "Rubbish bin"
        // into "cut".
        if (event.matches(StandardKey.Copy)) {
            root.putOnClipboard(false);
            event.accepted = true;
            return;
        }

        if (event.matches(StandardKey.Cut)) {
            root.putOnClipboard(true);
            event.accepted = true;
            return;
        }

        if (event.matches(StandardKey.Paste)) {
            root.mutController.paste();
            event.accepted = true;
            return;
        }

        const delta = root.cursorDelta(event.key);
        if (delta === undefined)
            return;

        root.navController.fileListModel.moveCursor(delta, event.modifiers);
        const row = root.navController.fileListModel.cursorRow();
        if (row >= 0)
            root.revealRow(row);
        event.accepted = true;
    }

    // hovered and pos are parameters rather than reads of viewHover so a test
    // can drive both sides without a window; the handler below supplies the real
    // ones. Same for the three below.
    function resolveHover(hovered, pos) {
        root.hoverRow = hovered ? root.rowAtPos(pos) : -1;
    }

    function handleLeftTap(pos, modifiers) {
        const row = root.rowAtPos(pos);
        // This handler only takes a passive grab, so it also fires for taps that
        // land inside the active rename field -- and takeFocus() below would
        // then commit the edit on the user's first click into their own text.
        // The renamed row is the cursor row by construction (see beginRename),
        // so that's the one to stand down for; a tap on any other row falls
        // through and commits via focus loss, which is the wanted behavior.
        if (root.renamingHandle !== 0 && row === root.navController.fileListModel.cursorRow())
            return;
        root.takeFocus();
        if (row < 0)
            root.navController.fileListModel.clearSelection();
        else
            root.navController.fileListModel.selectRow(row, modifiers);
    }

    // Right-click on empty space targets the folder the view is showing, not the
    // selection -- so it gets its own menu, and clears the selection first the
    // way the left button does. Same passive-grab caveat: a delegate's own
    // right-button handler fires as well as this one, so a tap that landed on a
    // row bails out here.
    function handleRightTap(pos) {
        if (root.rowAtPos(pos) >= 0)
            return;
        root.takeFocus();
        root.navController.fileListModel.clearSelection();
        backgroundMenu.popup();
    }

    // The delegates' right-button handlers reach the selection menu through
    // this, since the menu itself is no longer theirs to name.
    function popupContextMenu() {
        contextMenu.popup();
    }

    HoverHandler {
        id: viewHover
        parent: root.view
        onPointChanged: root.resolveHover(viewHover.hovered, viewHover.point.position)
        onHoveredChanged: root.resolveHover(viewHover.hovered, viewHover.point.position)
    }

    // Scrolling slides a different row under a stationary pointer, which the
    // handler above cannot see on its own (no point event is delivered).
    Connections {
        target: root.view
        function onContentYChanged() {
            root.resolveHover(viewHover.hovered, viewHover.point.position);
        }
    }

    TapHandler {
        parent: root.view
        acceptedButtons: Qt.LeftButton
        onTapped: root.handleLeftTap(point.position, point.modifiers)
    }

    TapHandler {
        parent: root.view
        acceptedButtons: Qt.RightButton
        onTapped: root.handleRightTap(point.position)
    }

    // Selection-driven, one instance for the whole view rather than one per
    // delegate (see FileContextMenu.qml's own comment) -- Menu is a Popup, not
    // an Item, so it is neither laid out by the view nor clipped by its
    // Flickable viewport, and a parentless popup() opens at the mouse cursor
    // regardless of where it was declared.
    FileContextMenu {
        id: contextMenu
        navController: root.navController
        onRenameRequested: root.beginRename()
        onMoveToRubbishRequested: confirmRubbishDialog.confirm()
    }

    FolderBackgroundMenu {
        id: backgroundMenu
        navController: root.navController
        mutController: root.mutController
        onNewFolderRequested: root.newFolderRequested()
    }

    ConfirmRubbishDialog {
        id: confirmRubbishDialog
        navController: root.navController
        mutController: root.mutController
    }
}
