import QtQuick
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

// Thumbnail-grid view for the grid-view mode -- extracted from Main.qml's
// central StackLayout at Phase 9 so TabContentPane.qml (one per tab) can
// instantiate an independent GridView per tab, same as FileTableView.qml's
// TableView already was its own file. Behavior is unchanged from the
// original inline GridView; only the controller/thumbnailController context
// properties (and window.activateEntry()) became navController/
// thumbController required properties plus activateRequested()/
// openInNewTabRequested() signals, since a tab's controllers are no longer
// singletons reachable by a fixed context-property name.
GridView {
    id: root

    required property var navController
    // See FileTableView.qml's note: mutations here, listing/selection/location
    // on navController above.
    required property var mutController
    required property var thumbController
    // Main.qml's window-wide DragProxy -- see its own comment for why the drag
    // is carried by a separate overlay item instead of a delegate.
    required property var dragProxy

    signal activateRequested(bool isFolder, var handle, string name, var sizeBytes)
    // Middle-click on a folder delegate below -- ignored for files, same
    // restriction as the "Open in new tab" context-menu action
    // (MenuActionResolver's FoldersOnly/SingleOnly spec).
    signal openInNewTabRequested(var handle)

    // Raised by the empty-space context menu, relayed by TabContentPane.qml to
    // the tab's single NewFolderDialog (one per tab, not per view -- see that
    // dialog's own comment).
    signal newFolderRequested

    // Off in the favourites listing, where every row carries the flag and the
    // marker would say nothing (FAVOURITES_VIEW_SPEC.md decision 6). Resolved
    // once here rather than per delegate.
    readonly property bool showFavouriteMarkers: root.navController?.viewKind
                                                 !== ViewKind.Favourites

    // Optional-chained: closing a tab clears the Repeater delegate's model
    // role before the pane is actually deleted, so navController is null for
    // the one binding re-evaluation in between.
    model: root.navController?.fileListModel ?? null
    clip: true
    // A cell is one tile plus the gap around it; the delegate fills the whole
    // cell and insets its visible tile by gap/2. That gap is dead space, not
    // part of the tile: indexAtViewportPos() below rejects it, so there is a
    // position between any two tiles where nothing is hovered (S8a). Width is
    // unchanged from the pre-S8 120 -- only the height grows, to make room for
    // the fixed thumbnail frame and a two-line name.
    cellWidth: Theme.grid.tileWidth + Theme.grid.gap
    cellHeight: Theme.grid.tileHeight + Theme.grid.gap
    // The other half of the gap at the top and bottom edges. Deliberately not
    // leftMargin/rightMargin: GridView's column count is floor(width /
    // cellWidth), which ignores those, so a horizontal margin just pushes the
    // last column out of view. If the left edge ever reads too tight, widen
    // Theme.grid.gap instead.
    topMargin: Theme.grid.gap / 2
    bottomMargin: Theme.grid.gap / 2
    // An overlay, so it takes nothing off root.width and the arrowColumns
    // binding below stays right.
    ScrollBar.vertical: ViewScrollBar {
        policy: ScrollBar.AsNeeded
    }
    // Same rationale as FileTableView.qml's TableView: Flickable defaults to
    // panning on left-drag, which since Phase 14a is how a move drag & drop
    // starts instead. NoButton disables drag/flick while leaving wheel
    // scrolling untouched (Flickable.acceptedButtons, since 6.9).
    acceptedButtons: Qt.NoButton
    // GridView has its own built-in arrow-key handling (currentIndex
    // movement + auto-scroll) that would otherwise fight with the selection
    // model driven by Keys.onPressed below.
    keyNavigationEnabled: false

    // The live InlineRenameField, published by its Loader below (null while not
    // renaming). Needed because GridView is itself a focus scope, unlike
    // FileTableView.qml's root -- see takeFocus().
    property Item activeRenameField: null

    // Moves active focus to the view for a mouse interaction. Plain
    // forceActiveFocus() isn't enough here: a focus scope hands active focus
    // straight back to its focused child, so while the rename field is up it
    // would never see a focus loss and its click-outside commit would never
    // fire. Dropping the field's focus first is what makes focus actually move.
    function takeFocus() {
        if (root.activeRenameField)
            root.activeRenameField.focus = false;
        root.forceActiveFocus();
    }

    // Attached properties only fire on the item that holds activeFocus, which
    // is this view and never the component below, so the attachment stays here.
    Keys.onPressed: event => viewInput.handleKey(event)

    // Index under a point given in view (viewport) coordinates, -1 past the
    // last tile *and* anywhere in the gap around one. Shared by every hit test
    // in this file -- tap, hover and drop must agree on which tile a position
    // belongs to, or clicking and highlighting drift apart, so the dead band
    // is resolved here rather than in the hover path alone (S8a). Every caller
    // already handles -1: a tap clears the selection, a drop falls back to the
    // folder this view is showing.
    function indexAtViewportPos(pos) {
        const contentPos = root.contentItem.mapFromItem(root, pos);
        const idx = root.indexAt(contentPos.x, contentPos.y);
        if (idx < 0)
            return -1;
        // Tested against the delegate's real geometry rather than by taking
        // contentPos modulo cellWidth/cellHeight, which would silently assume
        // the cell grid starts at content (0, 0).
        const item = root.itemAtIndex(idx);
        if (!item)
            return idx; // not realized; nothing to refine against
        const inset = Theme.grid.gap / 2;
        const dx = contentPos.x - item.x;
        const dy = contentPos.y - item.y;
        const insideTile = dx >= inset && dx < item.width - inset && dy >= inset && dy < item.height
              - inset;
        return insideTile ? idx : -1;
    }

    // Grid block a band rectangle (content coordinates) covers, as the
    // {firstRow, lastRow, columns, firstColumn, lastColumn} FileListModel's
    // updateBandSelectionGrid() takes -- (-1, -1) rows when it covers nothing.
    //
    // Arithmetic rather than indexAt()/itemAtIndex(): a band that auto-scrolls
    // reaches rows that were never realized, which those can't resolve. The
    // assumption that comes with it is that the cell grid starts at content
    // (0, 0) -- true for a GridView with no header, whose top/bottom margins
    // move the scroll range rather than the first cell.
    //
    // A tile is the cell minus the gap ring around it, the same inset every
    // other hit test in this file uses (S8a), so brushing the gutter between
    // two tiles selects neither.
    function bandBlock(contentRect) {
        const columns = Math.max(1, Math.floor(root.width / root.cellWidth));
        const inset = Theme.grid.gap / 2;

        const firstRow = Math.max(0, Math.floor((contentRect.y + inset) / root.cellHeight));
        const lastRow = Math.ceil((contentRect.y + contentRect.height - inset) / root.cellHeight)
              - 1;
        const firstColumn = Math.max(0, Math.floor((contentRect.x + inset) / root.cellWidth));
        const lastColumn = Math.min(columns - 1, Math.ceil((contentRect.x + contentRect.width
                                                            - inset) / root.cellWidth) - 1);

        if (lastRow < firstRow || lastColumn < firstColumn)
            return {
                "firstRow": -1,
                "lastRow": -1,
                "columns": columns,
                "firstColumn": 0,
                "lastColumn": 0
            };

        return {
            "firstRow": firstRow,
            "lastRow": lastRow,
            "columns": columns,
            "firstColumn": firstColumn,
            "lastColumn": lastColumn
        };
    }

    FileViewInput {
        id: viewInput
        view: root
        navController: root.navController
        mutController: root.mutController
        clipboard: clipboardController
        rowAtPos: pos => root.indexAtViewportPos(pos)
        // The `root.` is load-bearing: this view has a takeFocus() of its own,
        // so without it the lambda would call itself.
        takeFocus: () => root.takeFocus()
        revealRow: row => root.positionViewAtIndex(row, GridView.Contain)
        // Matches GridView's own FlowLeftToRight layout math; the vertical
        // ScrollBar is an overlay and takes nothing off the viewport width. A
        // binding, so a window resize is not a separate case.
        arrowColumns: Math.max(1, Math.floor(root.width / root.cellWidth))
        horizontalArrows: true
        onNewFolderRequested: root.newFolderRequested()
    }

    // See FileTableView.qml's copy for why both views listen and why the call is
    // deferred.
    Connections {
        target: root.navController
        function onRevealRowRequested(row) {
            Qt.callLater(viewInput.revealRow, row);
        }
    }

    FileViewDropArea {
        id: viewDrop
        view: root
        rowAtPos: pos => root.indexAtViewportPos(pos)
        navController: root.navController
        mutController: root.mutController
        dragProxy: root.dragProxy
        uploads: uploadController
        // The table's outline is square; nobody recorded why the two differ.
        outlineRadius: Theme.radius.sm
    }

    // Rubber-band selection (Phase 21). The gesture itself lives in the
    // component; what stays here is the grid geometry it can't know about.
    BandSelector {
        id: bandSelector
        view: root
        suppressed: viewInput.renamingHandle !== 0
        isOnItem: pos => root.indexAtViewportPos(pos) >= 0

        onBandStarted: additive => {
            root.takeFocus();
            root.navController.fileListModel.beginBandSelection(additive);
        }
        onBandChanged: contentRect => {
            const block = root.bandBlock(contentRect);
            root.navController.fileListModel.updateBandSelectionGrid(block.firstRow, block.lastRow,
                                                                     block.columns,
                                                                     block.firstColumn,
                                                                     block.lastColumn);
        }
        onBandFinished: root.navController.fileListModel.endBandSelection()
        onBandCanceled: root.navController.fileListModel.cancelBandSelection()
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
        required property bool isFavourite
        required property bool isExported

        readonly property bool renaming: viewInput.renamingHandle !== 0 && viewInput.renamingHandle
                                         === gridDelegateItem.handle

        readonly property bool dropTarget: viewDrop.dropRow === gridDelegateItem.index

        readonly property bool hovered: viewInput.hoverRow === gridDelegateItem.index

        // See FileTableView.qml's matching property for why this is bound to
        // the list rather than asked through a method.
        readonly property bool cutPending: clipboardController.cutHandles.indexOf(
                                               gridDelegateItem.handle) !== -1

        // Whether a real thumbnail image is what this tile shows. Folders never
        // have one, and a file that does still has an empty path until the
        // fetch below lands.
        readonly property bool hasImage: gridDelegateItem.hasThumbnail &&
                                         !gridDelegateItem.isFolder
                                         && gridDelegateItem.thumbnailPath !== ""

        width: GridView.view.cellWidth
        height: GridView.view.cellHeight

        Component.onCompleted: {
            if (gridDelegateItem.hasThumbnail && !gridDelegateItem.isFolder)
                root.thumbController.requestThumbnail(gridDelegateItem.handle);
        }

        // The visible tile: inset inside the cell so neighbours don't touch
        // (S8). Everything below is its child, so the fill never reaches the
        // gap -- and since S8a the hit test doesn't either, the same inset
        // being what indexAtViewportPos() rejects.
        Rectangle {
            id: tile
            anchors.fill: parent
            anchors.margins: Theme.grid.gap / 2
            radius: Theme.radius.sm
            color: gridDelegateItem.selected ? Theme.color.selection : (gridDelegateItem.hovered
                                                                        ? Theme.color.subtleHover :
                                                                          "transparent")
            // Outlined rather than filled, so a drop target that also happens
            // to be selected still reads as two distinct states.
            border.width: gridDelegateItem.dropTarget ? Theme.border.drop : 0
            border.color: Theme.color.accent

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.spacing.md
                spacing: Theme.spacing.sm

                // Fixed size in every tile, which is the whole point (S8): a
                // portrait and a landscape thumbnail used to be drawn at
                // wildly different sizes and positions. Opaque, so the
                // selection fill behind it never tints the image.
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: Theme.grid.thumbSize
                    Layout.preferredHeight: Theme.grid.thumbSize
                    // Ghosted per item rather than on the ColumnLayout: the
                    // rename editor shares that layout and must stay solid.
                    opacity: gridDelegateItem.cutPending ? Theme.opacity.cut : 1
                    radius: Theme.radius.sm
                    clip: true
                    color: gridDelegateItem.hasImage ? Theme.color.surface : "transparent"
                    // The frame is what marks an image out as an image; an icon
                    // tile is drawn bare, like Explorer's.
                    border.width: gridDelegateItem.hasImage ? Theme.border.thin : 0
                    border.color: gridDelegateItem.selected ? Theme.color.accent :
                                                              Theme.color.stroke

                    Image {
                        anchors.fill: parent
                        visible: gridDelegateItem.hasImage
                        // thumbnailPath uses native (backslash-on-Windows)
                        // separators -- normalize before building a URL.
                        source: gridDelegateItem.thumbnailPath ? ("file:///"
                                                                  + gridDelegateItem.thumbnailPath.replace(
                                                                      /\\/g, "/")) : ""
                        // Fills the frame instead of letterboxing inside it, so
                        // the drawn area is identical in every tile.
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }

                    FileIcon {
                        anchors.centerIn: parent
                        visible: !gridDelegateItem.hasImage
                        isFolder: gridDelegateItem.isFolder
                        fileName: gridDelegateItem.name
                        size: Theme.iconSize.lg
                    }

                    // Inside the frame, so it inherits both the clip and the
                    // cut ghosting above -- a cut item's heart fading with its
                    // thumbnail is the wanted reading.
                    Label {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: Theme.spacing.xs
                        visible: gridDelegateItem.isFavourite && root.showFavouriteMarkers
                        font.family: Theme.font.iconFamily
                        font.pixelSize: Theme.grid.markerGlyph
                        text: Theme.glyph.favourite
                        color: Theme.color.accent
                    }

                    // Opposite corner from the heart above so the two can never
                    // overlap on a tile carrying both. Shown on every screen --
                    // unlike the heart, no listing here is defined by this flag.
                    Label {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.spacing.xs
                        visible: gridDelegateItem.isExported
                        font.family: Theme.font.iconFamily
                        font.pixelSize: Theme.grid.markerGlyph
                        text: Theme.glyph.link
                        color: Theme.color.accent
                    }
                }

                Label {
                    visible: !gridDelegateItem.renaming
                    opacity: gridDelegateItem.cutPending ? Theme.opacity.cut : 1
                    Layout.fillWidth: true
                    // Fixed, not fillHeight: a one-line name must not pull the
                    // thumbnail frame off the line its neighbours sit on.
                    Layout.preferredHeight: Theme.grid.labelHeight
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignTop
                    // Two lines then "...", rather than the single ElideMiddle
                    // line that used to swallow all but a few characters of a
                    // Japanese name.
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    text: gridDelegateItem.name
                }

                // Takes the name label's slot in the tile. Inline Component for the
                // same required-property reason as FileTableView.qml's.
                Loader {
                    id: renameLoader
                    Layout.fillWidth: true
                    Layout.preferredHeight: Theme.grid.labelHeight
                    active: gridDelegateItem.renaming
                    // Only ever one active at a time, so this is the view's
                    // single handle on the live field (null again on unload).
                    onItemChanged: root.activeRenameField = renameLoader.item
                    sourceComponent: Component {
                        InlineRenameField {
                            originalName: gridDelegateItem.name
                            isFolder: gridDelegateItem.isFolder
                            onCommitted: newName => viewInput.commitRename(gridDelegateItem.handle,
                                                                           gridDelegateItem.name,
                                                                           newName)
                            onCancelled: Qt.callLater(viewInput.endRename)
                        }
                    }
                }
            }
        }

        // Left-click selection is handled entirely by FileViewInput's
        // view-level TapHandler -- this one is double-click-only.
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onDoubleTapped: root.activateRequested(gridDelegateItem.isFolder,
                                                   gridDelegateItem.handle, gridDelegateItem.name,
                                                   gridDelegateItem.sizeBytes)
        }

        // Starts a move drag. target: null because the tile must stay in the
        // grid -- what moves is Main.qml's DragProxy, which this only steers.
        // Passing the threshold makes this take the exclusive grab, which
        // cancels the view-level TapHandler's pending tap; that is what keeps
        // a drag off an already-selected tile from collapsing the selection.
        //
        // parent: tile, not the whole cell (Phase 21): the gap around a tile
        // is empty space for tap, hover and drop (S8a), so a drag starting
        // there has to fall through to the band selector rather than pick this
        // tile up. The handler stays declared here, beside its siblings.
        DragHandler {
            id: dragHandler
            parent: tile
            target: null

            onActiveChanged: {
                if (!dragHandler.active) {
                    root.dragProxy.finish();
                    return;
                }
                // Explorer's rule: dragging an unselected tile selects it
                // first, dragging a selected one carries the whole selection.
                if (!gridDelegateItem.selected) {
                    root.takeFocus();
                    root.navController.fileListModel.selectRow(gridDelegateItem.index,
                                                               Qt.NoModifier);
                }
                viewDrop.beginDrag(dragHandler.centroid.scenePosition);
            }

            // activeTranslation is the documented "changes on every move"
            // property; centroid is read for the position it changed to.
            onActiveTranslationChanged: if (dragHandler.active)
                                            root.dragProxy.moveTo(
                                                        dragHandler.centroid.scenePosition)

            onCanceled: root.dragProxy.cancel()
        }

        // Folder-only, mirrors FileTableView.qml's row delegate -- a file
        // has nothing sensible to open "in a new tab".
        TapHandler {
            acceptedButtons: Qt.MiddleButton
            onTapped: if (gridDelegateItem.isFolder)
                          root.openInNewTabRequested(gridDelegateItem.handle)
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: {
                if (!gridDelegateItem.selected) {
                    root.takeFocus();
                    root.navController.fileListModel.selectRow(gridDelegateItem.index,
                                                               Qt.NoModifier);
                }
                viewInput.popupContextMenu();
            }
        }
    }
}
