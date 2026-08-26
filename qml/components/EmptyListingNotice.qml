import QtQuick
// Before any other QtQuick.Controls import, same rule as its siblings.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// What a view shows when its listing holds nothing. Text only, no icon
// (FAVOURITES_VIEW_SPEC.md decision 9). It sits over the whole view area rather
// than inside either file view so the two don't each need a copy.
//
// Nothing here consumes mouse events -- Label doesn't, and no MouseArea is
// added -- so the background right-click the hint tells the user to perform
// still reaches the view underneath.
ColumnLayout {
    id: root

    required property var navController

    readonly property var listModel: root.navController?.fileListModel ?? null
    readonly property bool favourites: root.navController?.viewKind === ViewKind.Favourites
    readonly property bool recents: root.navController?.viewKind === ViewKind.Recents
    readonly property bool searching: root.navController?.searchActive ?? false

    // A folder listing is an in-memory read that lands before anything repaints, so
    // it needs no "has it loaded yet" guard. Favourites, recents and search do: they
    // walk the tree on a worker and empty the model at the click, and this would
    // otherwise answer "No favourites yet" about a listing nobody has fetched yet.
    visible: (root.listModel?.count ?? 0) === 0
             && !(root.navController?.listingPending ?? false)

    spacing: Theme.spacing.sm

    Label {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: Theme.color.textSecondary
        text: {
            if (root.searching) {
                if (root.favourites)
                    return qsTr("No favourites match your search");
                if (root.recents)
                    return qsTr("No recent items match your search");
                return qsTr("No items match your search");
            }
            if (root.favourites)
                return qsTr("No favourites yet");
            if (root.recents)
                return qsTr("Nothing was added in the last 30 days");
            return qsTr("This folder is empty");
        }
    }

    Label {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: Theme.color.textSecondary
        // Nothing on the Recent screen: no action a user can take makes an item
        // recent, so there is no instruction to give.
        visible: !root.searching && !root.recents
        // The menu rows' own wording, not a second copy of it: the two would
        // drift apart the first time either is reworded.
        text: root.favourites ? qsTr("Right-click a file or folder and choose \"%1\".").arg(
                                    ActionCatalog.entries["toggleFavourite"].label({
                                                                                       "favourited":
                                                                                       false
                                                                                   })) : qsTr(
                                    "Drop files here, or right-click and choose \"%1\".").arg(
                                    ActionCatalog.entries["newFolder"].label({}))
    }
}
