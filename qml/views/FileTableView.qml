import QtQuick
import QtQuick.Controls.FluentWinUI3
// HorizontalHeaderView/TableView are Qt Quick Controls types; the style
// import above re-exports them, but this import is kept explicit for
// qmllint/static-tooling clarity (same reasoning as Main.qml's directory
// import comment).
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// Explorer-style detail view for the list-view mode (Phase 6b/sort): a
// 3-column TableView (Name/Modified/Size -- a "Kind" column was considered
// but dropped, see MEMO.md's 2026-07-28 note: MegaApi::getChildren/search
// has no order value corresponding to it, and sorting here is deliberately
// server-side, not in-memory). Column header labels are hardcoded here
// rather than sourced from the model's headerData() -- this codebase's
// convention is "C++ passes structured fields, QML composes user-facing
// text" (see NotificationController/ErrorToast.qml), which also sidesteps an
// MSVC codepage gotcha with Japanese literals in .cpp/.h files.
//
// Clicking a header sorts by that column (first click ascending, repeat
// click toggles direction, same as Explorer); the chosen column/direction is
// forwarded to navController.setSortOrder(), which re-fetches from the SDK
// with the matching order rather than sorting in-memory.
//
// Sort order and column widths are persisted app-wide (last-write-wins
// across tabs, see TabContentPane.qml) rather than via a Settings item in
// this file directly -- Phase 9 made this a per-tab view (one live instance
// per open tab), and N tabs each owning a Settings item would fight over the
// same keys. initialSortColumn/initialSortAscending/initialColumnWidth*
// (required properties, set by TabContentPane from its own initial* values)
// seed this tab's starting point; sortOrderChanged/columnWidthsChanged relay
// this tab's own changes back out so TabContentPane can write them through
// to the single Settings instance (Main.qml).
ColumnLayout {
    id: root
    spacing: 0

    required property var navController
    // Main.qml's window-wide DragProxy -- see its own comment for why the drag
    // is carried by a separate overlay item instead of a delegate.
    required property var dragProxy

    // Re-exposed so TabContentPane (which owns activation/download dispatch)
    // can wire this file's double-click/download-open dispatch without this
    // file needing to know about that -- a plain `id` from a separately-
    // loaded QML file's object tree isn't reachable from here.
    signal activateRequested(bool isFolder, var handle, string name, var sizeBytes)
    // Middle-click on a folder row below -- ignored for files, same
    // restriction as the "Open in new tab" context-menu action
    // (FileActionResolver's FoldersOnly/SingleOnly spec).
    signal openInNewTabRequested(var handle)

    // See this file's own top comment: relayed up to TabContentPane/Main.qml
    // rather than written to a local Settings item.
    signal sortOrderChanged(int column, bool ascending)
    signal columnWidthsChanged(real nameWidth, real modifiedWidth, real sizeWidth)

    required property int initialSortColumn
    required property bool initialSortAscending
    required property real initialColumnWidthName
    required property real initialColumnWidthModified
    required property real initialColumnWidthSize

    readonly property var columnLabels: [qsTr("Name"), qsTr("Date modified"), qsTr("Size")]

    // Row under the pointer, shared by all three of its cells (S6). The
    // delegate is a cell, so a HoverHandler on it lights that cell alone --
    // every cell writes its own row here instead, and every cell reads it, so
    // the fill spans the row the way Explorer's does.
    property int hoverRow: -1

    // Drag & drop (Phase 14a), the mirror of FileGridView.qml's -- see the
    // comments there. The one difference is hit-testing: this view's delegate
    // is a cell, so a row is resolved through TableView.cellAtPosition rather
    // than GridView.indexAt.

    // Row a drop would land in, or -1 when it would land on the folder this
    // view is showing (empty space, a file row, or a folder that refuses it).
    property int dropRow: -1
    property bool dropOnCurrentFolder: false

    function beginDrag(scenePos) {
        const entries = root.navController.fileListModel.selectedEntries();
        if (entries.length === 0)
            return;
        const label = entries.length === 1 ? entries[0].name : qsTr("%1 items").arg(entries.length);
        root.dragProxy.begin(root.navController, entries.map(e => e.handle), label, scenePos);
    }

    function clearDropTarget() {
        root.dropRow = -1;
        root.dropOnCurrentFolder = false;
    }

    // Reads the payload off root.dragProxy rather than the event's own
    // drag.source, same reasoning as FileGridView.qml's.
    function updateDropTarget(drag, dropArea) {
        const pos = tableView.contentItem.mapFromItem(dropArea, Qt.point(drag.x, drag.y));
        // x clamped inside the last column so a hover to its right still hits
        // the row, matching the TapHandler's own full-row behavior. Clamping is
        // payload-agnostic, so it stays ahead of the branch below.
        const hit = tableView.cellAtPosition(Qt.point(Math.min(pos.x, tableView.contentWidth - 1),
                                                      pos.y), false);
        const entry = hit.y < 0 ? ({}) : root.navController.fileListModel.entryAt(hit.y);

        // Internal (move) vs. external (upload), same split as
        // FileGridView.qml's -- see FolderTreePanel.qml's DropArea for why the
        // guard is dragProxy.active rather than drag.hasUrls.
        if (root.dragProxy.active) {
            const nav = root.dragProxy.sourceNav;
            const handles = root.dragProxy.handles;

            if (entry.isFolder && nav.canDropHandlesOn(handles, entry.handle, false)) {
                root.dropRow = hit.y;
                root.dropOnCurrentFolder = false;
                return;
            }

            root.dropRow = -1;
            root.dropOnCurrentFolder = nav.canDropHandlesOn(handles,
                                                            root.navController.currentHandle,
                                                            root.navController.atRoot);
            return;
        }

        if (!drag.hasUrls) {
            root.clearDropTarget();
            drag.accepted = false;
            return;
        }

        if (entry.isFolder && uploadController.canUploadTo(entry.handle, false)) {
            root.dropRow = hit.y;
            root.dropOnCurrentFolder = false;
        } else {
            root.dropRow = -1;
            // Almost always true for an external drag (there's no "already
            // lives there" case), so the viewport frame stays lit for most of
            // the gesture -- Explorer's behavior, not a bug.
            root.dropOnCurrentFolder = uploadController.canUploadTo(root.navController.currentHandle,
                                                                    root.navController.atRoot);
        }
        // Only the external branch touches drag.accepted: the move path relies
        // on implicit acceptance by key match.
        drag.accepted = root.dropRow >= 0 || root.dropOnCurrentFolder;
    }

    // Matches (English) Windows Explorer's Date modified column: short date
    // + short time, e.g. "7/28/2026 3:45 PM". Not reproducible via
    // Qt.formatDateTime(date, Locale.ShortFormat) -- QLocale's own
    // ShortFormat for en_US is "M/d/yy h:mm AP" (2-digit year), while
    // Explorer's regional short-date default is 4-digit ("M/d/yyyy").
    // "AP" is Qt's AM/PM token (its "tt" is a timezone token, not AM/PM --
    // easy mix-up with .NET/Windows format-string syntax, where tt does
    // mean AM/PM).
    //
    // Wrapped in qsTr(), same as columnLabels above, so a Japanese .ts file
    // can later supply the equivalent Japanese Explorer format (typically
    // 24-hour, no AM/PM -- e.g. "yyyy/MM/dd H:mm") without any change here.
    readonly property string modifiedDateFormat: qsTr("M/d/yyyy h:mm AP")

    // Arbitrary picks, not derived from any content measurement -- just
    // enough to keep a dragged-in column from shrinking to unreadable/
    // zero-width. Tune by feel later if these turn out wrong.
    readonly property var minColumnWidths: [100, 100, 60]

    // 0 = Name, 1 = Modified, 2 = Size -- matches FileListModel::columnCount()
    // and FolderNavigationController::setSortOrder()'s column mapping.
    // Plain literal defaults (not bound to initialSortColumn/initialColumnWidth*
    // below) -- Component.onCompleted assigns the real starting value exactly
    // once, deliberately as a one-time imperative copy rather than a live
    // binding, so this tab's value doesn't keep tracking a sibling tab's
    // later changes to the shared Settings-backed value (see this file's top
    // comment and TabContentPane.qml's matching viewMode comment).
    property int sortColumn: 0
    property bool sortAscending: true

    // -1 = user has never explicitly resized this column (unset). In that
    // case columnWidthProvider below falls back to its hardcoded
    // [220, 150, 100]. real to match TableView.setColumnWidth()/
    // explicitColumnWidth()'s numeric type.
    property real columnWidthName: -1
    property real columnWidthModified: -1
    property real columnWidthSize: -1

    // Relays this tab's own sort-order/column-width changes back out (see
    // this file's top comment). Also fires once during Component.onCompleted
    // below when the initial* values differ from the literal defaults above
    // -- a harmless, idempotent echo of the value this tab just read.
    onSortColumnChanged: root.sortOrderChanged(root.sortColumn, root.sortAscending)
    onSortAscendingChanged: root.sortOrderChanged(root.sortColumn, root.sortAscending)
    onColumnWidthNameChanged: root.columnWidthsChanged(root.columnWidthName,
                                                       root.columnWidthModified,
                                                       root.columnWidthSize)
    onColumnWidthModifiedChanged: root.columnWidthsChanged(root.columnWidthName,
                                                           root.columnWidthModified,
                                                           root.columnWidthSize)
    onColumnWidthSizeChanged: root.columnWidthsChanged(root.columnWidthName,
                                                       root.columnWidthModified,
                                                       root.columnWidthSize)

    // Width of one of the two *fixed* columns -- persisted value if the user
    // has resized it, hardcoded default otherwise, clamped to its floor either
    // way. Column 0 is deliberately not expressible here; see
    // columnWidthProvider below.
    function fixedColumnWidth(column) {
        const w = tableView.explicitColumnWidth(column);
        // Math.max(min, w) pattern straight from Qt's own TableView docs
        // ("Row heights and column widths") for clamping a user-resizable
        // column to a floor.
        return Math.max(root.minColumnWidths[column], w >= 0 ? w : [220, 150, 100][column]);
    }

    // Same column same click: toggle direction. Different column: switch to
    // it, reset to ascending (Explorer's convention).
    function requestSort(column) {
        if (root.sortColumn === column)
            root.sortAscending = !root.sortAscending;
        else {
            root.sortColumn = column;
            root.sortAscending = true;
        }
        root.navController.setSortOrder(root.sortColumn, root.sortAscending);
    }

    // requestSort()'s counterpart for column widths. TableView has no
    // dedicated resize-finished signal, so layoutChanged (fired from
    // TableView below) is the only official hook -- but it also fires on
    // scrolling, unrelated to width. Safe to call unconditionally: a QML
    // property assignment that doesn't change the value is a no-op, so no
    // spurious writes happen. The >= 0 guard (explicitColumnWidth returns -1
    // when unset) also protects against a layoutChanged firing before
    // Component.onCompleted has restored persisted widths from clobbering
    // them with -1 -- nothing in this UI ever explicitly resets a column
    // back to unset.
    function saveColumnWidths() {
        const w0 = tableView.explicitColumnWidth(0);
        const w1 = tableView.explicitColumnWidth(1);
        const w2 = tableView.explicitColumnWidth(2);
        // Clamped to minColumnWidths here too (not just in
        // columnWidthProvider) so the persisted value already reflects what
        // the user actually sees, rather than the raw, possibly
        // sub-minimum, drag position.
        if (w0 >= 0)
            root.columnWidthName = Math.max(root.minColumnWidths[0], w0);
        if (w1 >= 0)
            root.columnWidthModified = Math.max(root.minColumnWidths[1], w1);
        if (w2 >= 0)
            root.columnWidthSize = Math.max(root.minColumnWidths[2], w2);
    }

    // Only sets a column's explicit width when a value was actually
    // persisted (>= 0) -- passing the -1 "unset" sentinel to
    // setColumnWidth() resets the column to content-based auto-width,
    // silently overriding columnWidthProvider's [220, 150, 100] fallback for
    // fresh installs.
    //
    // Called both from Component.onCompleted below and from
    // TableView.onRowsChanged: FileListModel::setEntries() (the initial
    // post-login folder load, and every subsequent folder navigation) does a
    // full beginResetModel()/endResetModel(), and TableView does not
    // guarantee it keeps explicitly-set column widths across a model reset.
    // Without reapplying on the rows-changed callback, a freshly logged-in
    // session would show the hardcoded default widths (not the persisted
    // ones) until the user resized a column again.
    function restoreColumnWidths() {
        if (root.columnWidthName >= 0)
            tableView.setColumnWidth(0, root.columnWidthName);
        if (root.columnWidthModified >= 0)
            tableView.setColumnWidth(1, root.columnWidthModified);
        if (root.columnWidthSize >= 0)
            tableView.setColumnWidth(2, root.columnWidthSize);
    }

    // The initial* required properties (set by TabContentPane from the
    // single shared Settings instance) are copied in here -- assignment
    // first, then navController.setSortOrder()/restoreColumnWidths() so the
    // initial fetch (once loadRoot() has actually happened -- see
    // FolderNavigationController::mHasLoadedOnce) uses the restored order.
    Component.onCompleted: {
        root.sortColumn = root.initialSortColumn;
        root.sortAscending = root.initialSortAscending;
        root.columnWidthName = root.initialColumnWidthName;
        root.columnWidthModified = root.initialColumnWidthModified;
        root.columnWidthSize = root.initialColumnWidthSize;
        root.navController.setSortOrder(root.sortColumn, root.sortAscending);
        root.restoreColumnWidths();
    }

    // Handle of the row currently being renamed in place, 0 when not renaming
    // (same meaningless-sentinel convention as PathSegment::isRoot's handle).
    // Only the name cell of that one row swaps its Label for an
    // InlineRenameField, via a Loader -- see the delegate below.
    property var renamingHandle: 0

    // Shared by F2 and the context menu's renameRequested. Renaming is
    // inherently single-item, so this collapses the selection to the cursor row
    // first -- selectRow(row, Qt.NoModifier) already means "deselect
    // everything else and select just this row", so FileListModel needed
    // nothing new. After a right-click the cursor is already on the clicked
    // row (the delegate's right-button TapHandler selects it), so both entry
    // points land here identically.
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
        tableView.positionViewAtRow(row, TableView.Contain);
    }

    // Focus must be handed back explicitly, same reason the focusPolicy:
    // Qt.NoFocus assignments elsewhere exist -- otherwise arrow keys go dead
    // once the field is gone.
    function endRename() {
        root.renamingHandle = 0;
        root.forceActiveFocus();
    }

    // Called from the field's committed signal, so tearing the field down is
    // deferred past the end of that signal's own handler.
    function commitRename(handle, oldName, newName) {
        if (newName !== oldName)
            root.navController.renameEntry(handle, newName);
        Qt.callLater(root.endRename);
    }

    // Placed on root (not tableView) so Main.qml has a single forceActiveFocus()
    // target per view, regardless of which child actually holds activeFocus.
    // Ctrl+A is handled here rather than via a window-level Shortcut so it
    // doesn't fire while the header search TextField has focus (Shortcut
    // ignores focus entirely and would steal Ctrl+A from text selection);
    // Keys.onPressed only fires for the item that currently has activeFocus.
    Keys.onPressed: event => {
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

        let delta = 0;
        if (event.key === Qt.Key_Up)
            delta = -1;
        else if (event.key === Qt.Key_Down)
            delta = 1;
        else
            return;

        root.navController.fileListModel.moveCursor(delta, event.modifiers);
        const row = root.navController.fileListModel.cursorRow();
        if (row >= 0)
            tableView.positionViewAtRow(row, TableView.Contain);
        event.accepted = true;
    }

    HorizontalHeaderView {
        id: header
        Layout.fillWidth: true
        syncView: tableView
        clip: true
        // textRole defaults to "display", which FileListModel::roleNames()
        // doesn't provide (this delegate below builds header text itself
        // from columnLabels, never via textRole) -- left unset it just spams
        // a "role doesn't exist" warning on every load. Qt treats "" as still
        // unset (it falls back to "display" internally for a
        // QAbstractItemModel), so silencing the warning needs an actual
        // existing role name, not just any explicit assignment -- "name" is
        // never read by the delegate below, it's only here to satisfy Qt's
        // roleNames() existence check.
        textRole: "name"
        // Same rationale as tableView below -- HorizontalHeaderView is also
        // a Flickable and would otherwise let click-drag pan it independently
        // of the synced tableView.
        acceptedButtons: Qt.NoButton

        delegate: Rectangle {
            id: headerCell
            implicitHeight: Theme.rowHeight.normal
            required property int column

            // FluentWinUI3 ships no HorizontalHeaderView delegate of its own
            // (unlike Basic), so this Rectangle's background is the only one
            // drawn here -- leaving `color` at Rectangle's default (opaque
            // white) clashed with the Label's palette-driven (theme-follows-
            // Windows) foreground, producing invisible white-on-white text.
            // transparent lets the real themed background show through,
            // same as the row delegate below.
            color: headerHover.hovered ? Theme.color.subtleHover : "transparent"

            // A header cell sorts on click, and nothing else here said so.
            HoverHandler {
                id: headerHover
            }

            // Explorer draws these inside the header only, never down the rows.
            // On the left edge rather than the right, so the last column ends
            // without a line butting up against the vertical scroll bar.
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.topMargin: Theme.spacing.sm
                anchors.bottomMargin: Theme.spacing.sm
                width: Theme.border.thin
                visible: headerCell.column > 0
                color: Theme.color.stroke
            }

            // A plain Row, not a RowLayout: Layout.fillWidth on the title would
            // push the sort chevron to the cell's right edge, and the decision
            // (S6-c) is to keep it right after the text. The title's width is
            // therefore explicit, so it still elides in a narrow column.
            Row {
                id: headerRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.spacing.md
                anchors.rightMargin: Theme.spacing.md
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacing.sm

                Label {
                    width: Math.min(implicitWidth, headerRow.width - (sortGlyph.visible
                                                                      ? sortGlyph.width
                                                                        + headerRow.spacing : 0))
                    font.pixelSize: Theme.font.body
                    color: Theme.color.text
                    elide: Text.ElideRight
                    text: root.columnLabels[headerCell.column]
                }

                Label {
                    id: sortGlyph
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.sortColumn === headerCell.column
                    font.family: Theme.font.iconFamily
                    font.pixelSize: Theme.font.caption
                    color: Theme.color.textSecondary
                    text: root.sortAscending ? Theme.glyph.chevronUp : Theme.glyph.chevronDown
                }
            }

            // Same TapHandler-on-a-child pattern as the row delegate below --
            // the Flickable's own acceptedButtons: Qt.NoButton (above) only
            // suppresses click-drag panning, not delivery to child handlers.
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onTapped: root.requestSort(headerCell.column)
            }
        }
    }

    // Sibling of the header rather than part of its delegate: delegates only
    // exist across contentWidth, so a line drawn there stops short whenever the
    // columns don't fill the viewport, and scrolls away when they overflow it.
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: Theme.border.thin
        color: Theme.color.stroke
    }

    TableView {
        id: tableView
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        resizableColumns: true
        // Flickable (TableView's base) defaults acceptedButtons to
        // Qt.LeftButton, i.e. click-drag pans the view -- unexpected for an
        // Explorer-style list. NoButton disables drag/flick while leaving
        // wheel scrolling untouched (Flickable.acceptedButtons, since 6.9).
        acceptedButtons: Qt.NoButton
        // Defensive: TableView's built-in key navigation needs a
        // selectionModel to do anything, which this view doesn't set, so
        // this is currently a no-op -- but explicit in case that changes.
        keyNavigationEnabled: false
        model: root.navController.fileListModel
        onLayoutChanged: root.saveColumnWidths()
        // rows changes on every FileListModel::setEntries() reset (initial
        // post-login load and folder navigation) -- see
        // restoreColumnWidths()'s comment for why explicit widths need
        // reapplying here, not just once at Component.onCompleted.
        onRowsChanged: root.restoreColumnWidths()

        // Name takes whatever the viewport has left over; Modified and Size
        // keep their own width. Two defects come out of the fixed-width
        // alternative, and both close here:
        //
        //  - this view's delegate is a *cell*, so the selection highlight is
        //    only ever as wide as the columns are. With fixed widths it ended
        //    partway across a wide window and left a dead strip to its right,
        //    which is also clickable (the TapHandler clamps into the last
        //    column) but never painted.
        //  - narrow windows had the opposite problem: the fixed total
        //    overflowed the viewport, and with no horizontal scroll bar the
        //    Size column simply could not be reached.
        //
        // The cost is that dragging Name's own edge no longer moves anything
        // -- resizing the other two is what widens or narrows it now.
        // columnWidthName is still saved and restored (that plumbing spans
        // TabContentPane and Main.qml's Settings), it just isn't read here.
        columnWidthProvider: function (column) {
            if (column !== 0)
                return root.fixedColumnWidth(column);
            return Math.max(root.minColumnWidths[0], tableView.width - root.fixedColumnWidth(1) - root.fixedColumnWidth(
                                2));

        }
        // The provider is only consulted during a layout pass, and resizing
        // the window alone doesn't trigger one.
        onWidthChanged: tableView.forceLayout()

        // Neither axis had one. Vertical: nothing indicated a folder had more
        // rows than fit. Horizontal: still reachable above the floor case,
        // when the user drags Modified or Size wide enough to overflow.
        ScrollBar.vertical: ScrollBar {}
        ScrollBar.horizontal: ScrollBar {}

        // The handler is declared inside TableView, which would install it on the
        // contentItem (Qt docs, TableView::cellAtPosition) -- i.e. it would never see
        // taps below the last row, since contentItem is only as tall as the content.
        // parent: tableView scopes it to the viewport instead, at the cost of having
        // to map the tap into content coordinates by hand.
        //
        // It also does the row selection, not just the clearing. The default
        // gesturePolicy (DragThreshold) takes a passive grab only, so a per-cell
        // TapHandler would fire in addition to this one and Ctrl+click would toggle
        // the same row twice, cancelling itself out.
        DragAutoScroller {
            id: autoScroller
            flickable: tableView
        }

        DropArea {
            id: tableDropArea
            // parent: tableView for the same reason as the TapHandler below --
            // a plain child of a Flickable lands in contentItem, which scrolls
            // and is only as tall as the content.
            parent: tableView
            anchors.fill: parent
            // "text/uri-list" is what an external OS drop matches on -- without
            // it those drops are silently ignored here.
            keys: ["application/x-megaexplorer-nodes", "text/uri-list"]

            onEntered: drag => {
                root.updateDropTarget(drag, tableDropArea);
                autoScroller.track(drag.y);
            }
            onPositionChanged: drag => {
                root.updateDropTarget(drag, tableDropArea);
                autoScroller.track(drag.y);
            }
            onExited: {
                root.clearDropTarget();
                autoScroller.release();
            }
            // Branches on drop.hasUrls, not on dragProxy.active like
            // updateDropTarget above -- see FolderTreePanel.qml's onDropped.
            onDropped: drop => {
                autoScroller.release();
                const target = root.dropRow >= 0 ? root.navController.fileListModel.entryAt(
                                                       root.dropRow).handle :
                                                   root.navController.currentHandle;
                const targetIsRoot = root.dropRow >= 0 ? false : root.navController.atRoot;

                if (root.dropRow >= 0 || root.dropOnCurrentFolder) {
                    if (drop.hasUrls) {
                        drop.accept(Qt.CopyAction);
                        uploadController.dropUrls(drop.urls, target, targetIsRoot);
                    } else {
                        root.dragProxy.sourceNav.moveHandlesTo(root.dragProxy.handles, target,
                                                               targetIsRoot);
                    }
                }
                root.clearDropTarget();
            }
        }

        // Drawn over the whole viewport when a drop would land in the folder
        // this view is showing, since that target has no delegate to highlight.
        Rectangle {
            parent: tableView
            anchors.fill: parent
            visible: root.dropOnCurrentFolder
            color: "transparent"
            border.width: Theme.border.drop
            border.color: Theme.color.accent
        }

        TapHandler {
            parent: tableView
            acceptedButtons: Qt.LeftButton
            onTapped: {
                const pos = tableView.contentItem.mapFromItem(tableView, point.position);
                // x clamped inside the last column so a tap to its right still hits
                // the row, matching Explorer's full-row selection.
                const hit = tableView.cellAtPosition(Qt.point(Math.min(pos.x, tableView.contentWidth
                                                                       - 1), pos.y), false);
                // This handler only takes a passive grab, so it also fires for
                // taps that land inside the active rename field -- and
                // forceActiveFocus() below would then commit the edit on the
                // user's first click into their own text. The renamed row is the
                // cursor row by construction (see beginRename), so that's the
                // one to stand down for; a tap on any other row falls through
                // and commits via focus loss, which is the wanted behavior.
                if (root.renamingHandle !== 0 && hit.y
                        === root.navController.fileListModel.cursorRow())
                    return;
                root.forceActiveFocus();
                if (hit.y < 0)
                    root.navController.fileListModel.clearSelection();
                else
                    root.navController.fileListModel.selectRow(hit.y, point.modifiers);
            }
        }

        delegate: Rectangle {
            id: cell
            implicitHeight: Theme.rowHeight.normal
            required property int row
            required property int column
            required property string name
            required property bool isFolder
            required property var handle
            required property var sizeBytes
            required property string formattedSize
            required property double modificationTime
            required property bool selected

            // Only the name column of the one renamed row turns into an editor.
            readonly property bool renaming: root.renamingHandle !== 0 && root.renamingHandle
                                             === cell.handle && cell.column === 0

            // No rounded corners here, unlike the side panel's pill (S5-a):
            // this delegate is a cell, so a radius would round off the middle
            // of a row at every column boundary.
            color: cell.selected ? Theme.color.selection : (root.hoverRow === cell.row
                                                            ? Theme.color.subtleHover :
                                                              "transparent")

            // Writes the shared row (see root.hoverRow): the last cell to lose
            // the pointer clears it, and a cell that lost it after another one
            // already claimed a different row leaves that claim alone.
            HoverHandler {
                id: cellHover
                onHoveredChanged: {
                    if (cellHover.hovered)
                        root.hoverRow = cell.row;
                    else if (root.hoverRow === cell.row)
                        root.hoverRow = -1;
                }
            }

            // Outlined rather than filled, so a drop target that also happens
            // to be selected still reads as two distinct states. Top/bottom
            // only, so adjacent cells of the same row join into one band.
            Rectangle {
                anchors.fill: parent
                visible: root.dropRow === cell.row
                color: "transparent"
                border.width: Theme.border.drop
                border.color: Theme.color.accent
            }

            // The icon is a sibling of the label rather than a prefix on its
            // text: as part of the string it rode on Segoe UI Emoji (colour,
            // against an otherwise monochrome icon set), sat a single space
            // away from the name, and got eaten by ElideMiddle on a narrow
            // column (4-4).
            RowLayout {
                visible: !cell.renaming
                anchors.fill: parent
                // Horizontal only, and the same token the header uses (S6-a):
                // the two used to be 4 and 6, which left the header text 2px off
                // the row content it labels. Vertical padding falls out of the
                // 32px row height around a 16px icon, so it isn't spelled here.
                anchors.leftMargin: Theme.spacing.md
                anchors.rightMargin: Theme.spacing.md
                spacing: Theme.spacing.md

                FileIcon {
                    // An invisible item is skipped by RowLayout entirely, so
                    // the date and size columns keep their full width.
                    visible: cell.column === 0
                    isFolder: cell.isFolder
                }

                Label {
                    Layout.fillWidth: true
                    font.pixelSize: Theme.font.body
                    color: Theme.color.text
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: cell.column === 2 ? Text.AlignRight : Text.AlignLeft
                    elide: Text.ElideMiddle
                    text: {
                        switch (cell.column) {
                        case 0:
                            return cell.name;
                        case 1:
                            // Folders have no modification time from the SDK
                            // (MegaNode::getModificationTime() returns 0 for
                            // them) -- formatting that would show the Epoch
                            // instead of a blank cell, unlike Explorer.
                            return cell.isFolder ? "" : Qt.formatDateTime(new Date(
                                                                              cell.modificationTime
                                                                              * 1000), root.modifiedDateFormat);
                        case 2:
                            return cell.formattedSize;
                        }
                        return "";
                    }
                }
            }

            // The Component is declared inline rather than shared at root level
            // because InlineRenameField's properties are `required`, and a
            // Loader has no way to supply those to a component it didn't
            // declare -- inline, they can just bind to `cell`. Component itself
            // instantiates nothing until active turns true.
            Loader {
                anchors.fill: parent
                anchors.margins: Theme.spacing.xs
                active: cell.renaming
                sourceComponent: Component {
                    InlineRenameField {
                        originalName: cell.name
                        isFolder: cell.isFolder
                        onCommitted: newName => root.commitRename(cell.handle, cell.name, newName)
                        onCancelled: Qt.callLater(root.endRename)
                    }
                }
            }

            // Left-click selection is handled entirely by the view-level
            // background TapHandler above (see its comment) -- this one is
            // double-click-only.
            TapHandler {
                acceptedButtons: Qt.LeftButton
                onDoubleTapped: root.activateRequested(cell.isFolder, cell.handle, cell.name,
                                                       cell.sizeBytes)
            }

            // Per cell rather than per row -- the delegate is a cell here, so
            // this is what makes the whole row draggable. See
            // FileGridView.qml's matching handler for the rest of the reasoning.
            DragHandler {
                id: dragHandler
                target: null

                onActiveChanged: {
                    if (!dragHandler.active) {
                        root.dragProxy.finish();
                        return;
                    }
                    if (!cell.selected) {
                        root.forceActiveFocus();
                        root.navController.fileListModel.selectRow(cell.row, Qt.NoModifier);
                    }
                    root.beginDrag(dragHandler.centroid.scenePosition);
                }

                onActiveTranslationChanged: if (dragHandler.active)
                                                root.dragProxy.moveTo(
                                                            dragHandler.centroid.scenePosition)

                onCanceled: root.dragProxy.cancel()
            }

            // Folder-only, mirrors FileGridView.qml's delegate -- a file has
            // nothing sensible to open "in a new tab".
            TapHandler {
                acceptedButtons: Qt.MiddleButton
                onTapped: if (cell.isFolder)
                              root.openInNewTabRequested(cell.handle)
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                onTapped: {
                    if (!cell.selected) {
                        root.forceActiveFocus();
                        root.navController.fileListModel.selectRow(cell.row, Qt.NoModifier);
                    }
                    contextMenu.popup();
                }
            }
        }
    }

    // Selection-driven, one instance for the whole view rather than one per
    // delegate cell (see FileContextMenu.qml's own comment) -- Menu is a
    // Popup, not an Item, so it's neither laid out by this ColumnLayout nor
    // clipped by TableView's Flickable viewport; a parentless popup() opens
    // at the mouse cursor regardless.
    FileContextMenu {
        id: contextMenu
        navController: root.navController
        onRenameRequested: root.beginRename()
        onMoveToRubbishRequested: confirmRubbishDialog.confirm()
    }

    // One per view, same reasoning as the menu above -- the action always
    // targets this view's own selection.
    ConfirmRubbishDialog {
        id: confirmRubbishDialog
        navController: root.navController
    }
}
