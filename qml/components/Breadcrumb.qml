import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3

// Windows-Explorer-style breadcrumb trail. model is
// FolderNavigationController::breadcrumb -- a QVariantList of
// {name, handle, isRoot}, root-first, current folder last. Each delegate is
// a segment (clickable, navigates there) plus its own independent chevron
// separator -- kept separate now so a later phase can attach a
// TapHandler/Menu to just the separator (subfolder dropdown) without
// restructuring the delegate.
//
// Overflow: when the trail doesn't fit root.width, segments are hidden from
// the left (root) side and replaced with a leading "«". The cutoff index is
// computed directly from each delegate's implicitWidth (stable regardless of
// visible state) rather than by iteratively toggling visible and re-reading
// Row.implicitWidth, which wouldn't converge in one pass.
Item {
    id: root
    clip: true

    required property var navController
    required property var dragProxy

    property alias model: repeater.model

    // Inside the frame, before the first segment's own 4px -- so the frame's
    // inner edge sits 8px from the leading glyph, matching Explorer.
    readonly property int contentPadding: Theme.spacing.sm

    // Only a fallback: in the toolbar the frame is sized to the search field
    // beside it (Main.qml). The inner row is 28, smaller than the frame, so it
    // can't be what states this any more.
    implicitHeight: Theme.rowHeight.normal

    // Index of the first (leftmost) segment still shown; segments before it
    // are hidden and represented by the "«" indicator. The last segment
    // (current folder) is always kept, eliding its label instead if needed.
    property int firstVisibleIndex: 0

    function relayout() {
        const n = repeater.count;
        if (n === 0) {
            root.firstVisibleIndex = 0;
            return;
        }

        let idx = 0;
        while (idx < n - 1) {
            let total = (idx > 0 ? overflowIndicator.implicitWidth : 0);
            for (let i = idx; i < n; ++i) {
                const item = repeater.itemAt(i);
                if (item)
                    total += item.implicitWidth;
            }
            if (total <= root.width - 2 * root.contentPadding)
                break;
            ++idx;
        }
        root.firstVisibleIndex = idx;
    }

    onWidthChanged: Qt.callLater(root.relayout)

    // Explorer draws its address bar as a field, not as bare text on the band
    // (S8b). Declared before the Row so it paints underneath it without a z
    // fight. fieldFill is the SearchField sprite's own fill, so the two boxes
    // read as the same kind of surface; the border stays a flat stroke where
    // the sprite has a top-to-bottom gradient (5.9%->15.7% black in light),
    // which a Rectangle can't express and which is invisible at this size.
    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.sm
        color: Theme.color.fieldFill
        border.width: Theme.border.thin
        border.color: Theme.color.stroke
    }

    Row {
        id: row
        anchors.left: parent.left
        anchors.leftMargin: root.contentPadding
        anchors.verticalCenter: parent.verticalCenter

        Label {
            id: overflowIndicator
            visible: root.firstVisibleIndex > 0
            // Row has no vertical alignment of its own, so every direct child
            // has to carry the same height or it drifts on its own (S8a).
            height: Theme.rowHeight.compact
            verticalAlignment: Text.AlignVCenter
            leftPadding: Theme.spacing.sm
            rightPadding: Theme.spacing.sm
            text: "«"
        }

        Repeater {
            id: repeater
            onItemAdded: Qt.callLater(root.relayout)
            onItemRemoved: Qt.callLater(root.relayout)

            delegate: Row {
                id: delegateRoot
                required property int index
                required property var modelData

                visible: delegateRoot.index >= root.firstVisibleIndex

                // The last segment is the current folder: nothing to navigate
                // to, so it gets neither the hover pill nor a separator after
                // it. Same condition the TapHandler below is gated on.
                readonly property bool navigable: delegateRoot.index < repeater.count - 1

                Item {
                    id: segment
                    // Width is sized off the inner Row rather than the other way
                    // round, so this stays a plain Item the pill can
                    // anchors.fill. Height is the token, *not* the content: Row
                    // top-aligns its children, so a content-derived height left
                    // the root segment (16px cloud glyph) 2px taller than the
                    // rest and parted their baselines (S8a). One height for
                    // every segment makes the alignment structural.
                    implicitWidth: segmentContent.implicitWidth + 2 * Theme.spacing.sm
                    implicitHeight: Theme.rowHeight.compact

                    // Hover pill and drop feedback in one rectangle -- two
                    // stacked backgrounds would double the border in the state
                    // where both are on. Label has no background of its own and
                    // a plain child would paint over the text, hence z: -1;
                    // anchors.fill keeps it out of relayout()'s implicitWidth
                    // sum, which the overflow cutoff depends on.
                    Rectangle {
                        anchors.fill: parent
                        z: -1
                        radius: Theme.radius.sm
                        color: (hoverHandler.hovered && delegateRoot.navigable)
                               ? Theme.color.subtleHover : "transparent"
                        border.width: dropArea.accepting ? Theme.border.drop : 0
                        border.color: Theme.color.accent
                    }

                    Row {
                        id: segmentContent
                        anchors.centerIn: parent
                        spacing: Theme.spacing.md

                        // Inside the delegate rather than beside the trail: when
                        // overflow folds the root away behind "«", the icon has
                        // to go with it (S7-d).
                        //
                        // Both children carry the same explicit height for the
                        // reason the outer Row's children do -- S8a fixed that
                        // level and missed this one (S8b). A 16px icon font and
                        // a 14px text font have different ascents, so without a
                        // shared box they top-align and the cloud rides ~2px
                        // high.
                        Label {
                            visible: delegateRoot.modelData.isRoot
                            height: Theme.rowHeight.compact
                            verticalAlignment: Text.AlignVCenter
                            font.family: Theme.font.iconFamily
                            font.pixelSize: Theme.iconSize.sm
                            color: Theme.color.textSecondary
                            text: Theme.glyph.cloud
                        }

                        Label {
                            height: Theme.rowHeight.compact
                            verticalAlignment: Text.AlignVCenter
                            // Fluent's own default, stated so the metrics this
                            // alignment depends on aren't implicit.
                            font.pixelSize: Theme.font.body
                            elide: Text.ElideRight
                            text: delegateRoot.modelData.isRoot ? qsTr("Cloud Drive") :
                                                                  delegateRoot.modelData.name
                        }
                    }

                    HoverHandler {
                        id: hoverHandler
                    }

                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        enabled: delegateRoot.navigable
                        onTapped: root.navController.navigateTo(delegateRoot.modelData.handle,
                                                                delegateRoot.modelData.isRoot)
                    }

                    // Shared drop behaviour lives in NodeDropArea.qml; this
                    // segment only says which node it stands for. Segments
                    // hidden behind "«" are invisible and so receive no drag
                    // events -- deliberately not reachable as drop targets.
                    //
                    // The last segment behaves differently from the other drop
                    // sites: it's the current folder, which checkMove rejects as
                    // "already in that folder", but which is a perfectly valid
                    // upload destination -- so it highlights for an external
                    // drop and not for a move. That asymmetry is correct.
                    // A Ctrl+drag *copy* also highlights it, on purpose: copying
                    // into the folder the nodes already live in duplicates them
                    // under "... - Copy", which is a real request.
                    NodeDropArea {
                        id: dropArea
                        anchors.fill: parent
                        dragProxy: root.dragProxy
                        uploads: uploadController
                        targetHandle: delegateRoot.modelData.handle
                        targetIsRoot: delegateRoot.modelData.isRoot
                    }
                }

                // Independent element (not part of the segment above) so a
                // later phase can hang a subfolder-listing TapHandler/Menu
                // off just this separator without touching it.
                Label {
                    visible: delegateRoot.navigable
                    // Row top-aligns its children, and this one is shorter than
                    // the segment beside it, so it has to be stretched to match
                    // before verticalAlignment can centre the glyph.
                    height: segment.height
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: Theme.spacing.xs
                    rightPadding: Theme.spacing.xs
                    font.family: Theme.font.iconFamily
                    // Deliberately below body size: the chevron is a separator,
                    // not a peer of the names either side of it.
                    font.pixelSize: Theme.font.caption
                    color: Theme.color.textSecondary
                    text: Theme.glyph.chevronRight
                }
            }
        }
    }

    Connections {
        target: repeater
        function onModelChanged() {
            Qt.callLater(root.relayout);
        }
    }
}
