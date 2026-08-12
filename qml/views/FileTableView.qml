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
// text" (see NotificationController/ToastStack.qml), which also sidesteps an
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
    // This tab's FileMutationController -- everything that changes the remote
    // tree (rename, paste, drag-move/copy) goes through it, while the listing,
    // selection and location stay on navController above.
    required property var mutController
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
    // (MenuActionResolver's FoldersOnly/SingleOnly spec).
    signal openInNewTabRequested(var handle)

    // Raised by the empty-space context menu, relayed by TabContentPane.qml to
    // the tab's single NewFolderDialog (one per tab, not per view -- see that
    // dialog's own comment).
    signal newFolderRequested

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

    // Off in the favourites listing, where every row carries the flag and the
    // marker would say nothing (FAVOURITES_VIEW_SPEC.md decision 6). Resolved
    // once here rather than per delegate.
    readonly property bool showFavouriteMarkers: root.navController?.viewKind
                                                 !== ViewKind.Favourites

    // Row under a point given in tableView (viewport) coordinates, -1 past the
    // last row and -1 anywhere right of the last column: that strip is empty
    // space, not part of the row. Reverses S6a-a, which had clamped x into the
    // last column to give Explorer's full-row hit area
    // (docs/DESIGN_IMPROVEMENT.md 4-1) -- the fill below stops at the same edge,
    // so the two still agree.
    function rowAt(viewportPos) {
        const pos = tableView.contentItem.mapFromItem(tableView, viewportPos);
        if (pos.x >= tableView.contentWidth)
            return -1;
        return tableView.cellAtPosition(pos, false).y;
    }

    // Rows a band rectangle (content coordinates) covers, as the {firstRow,
    // lastRow} pair FileListModel's updateBandSelection() takes -- (-1, -1)
    // when it covers none.
    //
    // Arithmetic rather than rowAt()/cellAtPosition(), which only resolve
    // loaded cells: a band that auto-scrolls reaches rows that were never
    // loaded. Rows are uniform (the delegate's implicitHeight is the only
    // thing that sets their height) and the first one starts at content y 0 --
    // the header is a separate view above this one, not a header row inside it.
    //
    // x is deliberately not consulted, unlike rowAt() above: a band dragged
    // down the empty strip right of the last column still catches every row it
    // spans vertically, which is the whole point of that strip being empty.
    function bandRows(contentRect) {
        const rowHeight = Theme.rowHeight.normal;
        const firstRow = Math.max(0, Math.floor(contentRect.y / rowHeight));
        const lastRow = Math.ceil((contentRect.y + contentRect.height) / rowHeight) - 1;
        return lastRow < firstRow ? {
                                        "firstRow": -1,
                                        "lastRow": -1
                                    } : {
            "firstRow": firstRow,
            "lastRow": lastRow
        };
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

    // Used until the user resizes a column. Name's is only ever seen when
    // fitNameColumnOnce() below can't run (a zero-width viewport); normally
    // that fit supersedes it on the very first layout.
    readonly property var defaultColumnWidths: [220, 150, 100]

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
    // case columnWidthFor() below falls back to defaultColumnWidths. real to
    // match TableView.setColumnWidth()/explicitColumnWidth()'s numeric type.
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

    // Width of any column -- persisted value if the user has resized it,
    // default otherwise, clamped to its floor either way. All three go through
    // here; see columnWidthProvider below for why none of them is derived from
    // the viewport.
    function columnWidthFor(column) {
        const w = tableView.explicitColumnWidth(column);
        const fallback = root.defaultColumnWidths[column];
        // Math.max(min, w) pattern straight from Qt's own TableView docs
        // ("Row heights and column widths") for clamping a user-resizable
        // column to a floor.
        return Math.max(root.minColumnWidths[column], w >= 0 ? w : fallback);
    }

    // Fresh install (nothing persisted): Explorer opens with Name filling most
    // of the window, not a 220px column and 700px of empty space to its right.
    // Runs at most once, and only for Name -- afterwards the column is an
    // ordinary user-resizable width like the other two, and the value lands in
    // Settings through onColumnWidthNameChanged like any manual resize, so a
    // second tab or a later session finds it already set and skips this.
    property bool nameWidthInitialized: false

    function fitNameColumnOnce() {
        if (root.nameWidthInitialized || !root.completed || tableView.width <= 0)
            return;
        root.nameWidthInitialized = true;
        if (root.columnWidthName >= 0)
            return;
        const rest = root.columnWidthFor(1) + root.columnWidthFor(2);
        root.columnWidthName = Math.max(root.minColumnWidths[0], tableView.width - rest);
        tableView.setColumnWidth(0, root.columnWidthName);
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
    // silently overriding columnWidthFor()'s defaultColumnWidths fallback for
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

    // Guards fitNameColumnOnce() against running before the persisted widths
    // below have been copied in -- TableView.onWidthChanged is the other call
    // site and there is no ordering guarantee between the two.
    property bool completed: false

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
        root.completed = true;
        root.fitNameColumnOnce();
    }

    // Attached properties only fire on the item that holds activeFocus, and
    // this root is the view's single focus target -- so the attachment stays
    // here while everything it decides lives in FileViewInput.
    //
    // Ctrl+A goes through it rather than a window-level Shortcut so it doesn't
    // fire while the header search TextField has focus (Shortcut ignores focus
    // entirely and would steal Ctrl+A from text selection).
    Keys.onPressed: event => viewInput.handleKey(event)

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
        // Optional-chained: closing a tab clears the Repeater delegate's model
        // role before the pane is actually deleted, so navController is null
        // for the one binding re-evaluation in between.
        model: root.navController?.fileListModel ?? null
        onLayoutChanged: root.saveColumnWidths()
        // rows changes on every FileListModel::setEntries() reset (initial
        // post-login load and folder navigation) -- see
        // restoreColumnWidths()'s comment for why explicit widths need
        // reapplying here, not just once at Component.onCompleted.
        onRowsChanged: root.restoreColumnWidths()

        // Every column is its own width, none derived from the viewport --
        // Explorer's arrangement, and the only one where both resize handles
        // work. S0 briefly had Name absorb the leftover width instead, which
        // broke resizing outright: a handle writes the explicit width of the
        // column *left* of the boundary, so a column the provider derives can
        // never be dragged, and its neighbour appears to move the wrong way
        // (DESIGN_IMPROVEMENT.md 4-5).
        //
        // The two problems that arrangement had solved are handled elsewhere
        // now: a total wider than the viewport is what the horizontal scroll
        // bar is for, and the strip left over when the columns are narrower is
        // deliberately empty space rather than part of the row (see rowAt).
        columnWidthProvider: column => root.columnWidthFor(column)
        onWidthChanged: root.fitNameColumnOnce()

        // Neither axis had one. Vertical: nothing indicated a folder had more
        // rows than fit; the wheel still scrolls it whether or not the bar is
        // showing, so the default fade-in-when-active behaviour is fine.
        ScrollBar.vertical: ViewScrollBar {}
        // Horizontal is not the same case: the wheel only scrolls vertically
        // and drag-panning is off (acceptedButtons above), so this bar is the
        // *only* way to reach a column past the right edge -- and a bar that
        // only appears once the view is already moving can never be the thing
        // that starts the movement. Pinned on whenever the columns overflow,
        // which S6a made an ordinary state rather than an edge case (they no
        // longer shrink to fit). Explorer 11 shows a persistent bar here too.
        ScrollBar.horizontal: ViewScrollBar {
            policy: tableView.contentWidth > tableView.width ? ScrollBar.AlwaysOn :
                                                               ScrollBar.AlwaysOff
        }

        FileViewDropArea {
            id: viewDrop
            view: tableView
            rowAtPos: pos => root.rowAt(pos)
            navController: root.navController
            mutController: root.mutController
            dragProxy: root.dragProxy
            uploads: uploadController
        }

        FileViewInput {
            id: viewInput
            view: tableView
            navController: root.navController
            mutController: root.mutController
            clipboard: clipboardController
            rowAtPos: pos => root.rowAt(pos)
            // root, not tableView: this ColumnLayout is the view's single focus
            // target, so Main.qml has one to aim at whatever holds activeFocus.
            takeFocus: () => root.forceActiveFocus()
            revealRow: row => tableView.positionViewAtRow(row, TableView.Contain)
            // One row per arrow, and Left/Right deliberately left unaccepted --
            // see FileViewInput's cursorDelta().
            arrowColumns: 1
            horizontalArrows: false
            onNewFolderRequested: root.newFolderRequested()
        }

        // Scrolling a row the controller already selected into view -- e.g. a
        // folder that was just created. Both views of a tab listen, so the
        // hidden one is positioned too and switching view mode doesn't land
        // somewhere else. Connections is a QtObject, so this ColumnLayout does
        // not lay it out.
        //
        // callLater, not a direct call: the signal is emitted straight after the
        // model reset that produced the row, and both views rebuild their item
        // geometry in a later polish pass -- positioning against the old one can
        // land short.
        Connections {
            target: root.navController
            function onRevealRowRequested(row) {
                Qt.callLater(viewInput.revealRow, row);
            }
        }

        // Rubber-band selection (Phase 21). The strip right of the last column
        // is empty space here as it is everywhere else, so rowAt() alone
        // answers this: it carries no delegate and no DragHandler, leaving a
        // press-drag there nothing else to mean.
        BandSelector {
            id: bandSelector
            view: tableView
            suppressed: viewInput.renamingHandle !== 0
            isOnItem: pos => root.rowAt(pos) >= 0

            onBandStarted: additive => {
                root.forceActiveFocus();
                root.navController.fileListModel.beginBandSelection(additive);
            }
            onBandChanged: contentRect => {
                const rows = root.bandRows(contentRect);
                root.navController.fileListModel.updateBandSelection(rows.firstRow, rows.lastRow);
            }
            onBandFinished: root.navController.fileListModel.endBandSelection()
            onBandCanceled: root.navController.fileListModel.cancelBandSelection()
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
            required property bool isFavourite

            // Only the name column of the one renamed row turns into an editor.
            readonly property bool renaming: viewInput.renamingHandle !== 0
                                             && viewInput.renamingHandle === cell.handle
                                             && cell.column === 0

            // Bound to the list rather than asked through a method: a method
            // call reads no property, so the binding would never re-evaluate
            // when the clipboard changes. Handles compare exactly -- both sides
            // take the same quint64-to-JS-number path, and MEGA's are 48-bit.
            readonly property bool cutPending: clipboardController.cutHandles.indexOf(cell.handle)
                                               !== -1

            // No rounded corners here, unlike the side panel's pill (S5-a):
            // this delegate is a cell, so a radius would round off the middle
            // of a row at every column boundary.
            color: cell.selected ? Theme.color.selection : (viewInput.hoverRow === cell.row
                                                            ? Theme.color.subtleHover :
                                                              "transparent")

            // Outlined rather than filled, so a drop target that also happens
            // to be selected still reads as two distinct states. Drawn once
            // per row by its first cell, spanning every column -- one per cell
            // would put a vertical line at every column boundary.
            Rectangle {
                visible: viewDrop.dropRow === cell.row && cell.column === 0
                width: tableView.contentWidth - cell.x
                height: parent.height
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
                // On the content, not the cell: the selection fill and the drop
                // outline stay solid, and the rename editor is a sibling, so it
                // is never ghosted.
                opacity: cell.cutPending ? Theme.opacity.cut : 1
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
                    fileName: cell.name
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

                // Right edge of the name column, since the Label above takes
                // all the slack. No disc behind it, unlike the grid's badge:
                // the backdrop here is a flat row, not a photograph.
                Label {
                    visible: cell.column === 0 && cell.isFavourite && root.showFavouriteMarkers
                    font.family: Theme.font.iconFamily
                    font.pixelSize: Theme.font.caption
                    color: Theme.color.accent
                    verticalAlignment: Text.AlignVCenter
                    text: Theme.glyph.favourite
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
                        onCommitted: newName => viewInput.commitRename(cell.handle, cell.name,
                                                                       newName)
                        onCancelled: Qt.callLater(viewInput.endRename)
                    }
                }
            }

            // Left-click selection is handled entirely by FileViewInput's
            // view-level TapHandler -- this one is double-click-only.
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
                    viewDrop.beginDrag(dragHandler.centroid.scenePosition);
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
                    viewInput.popupContextMenu();
                }
            }
        }
    }
}
