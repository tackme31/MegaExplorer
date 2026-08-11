import QtQuick
// Before any other QtQuick.Controls import, same rule as its siblings.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Layouts

// What a view shows when its listing holds nothing. Text only, no icon
// (FAVOURITES_VIEW_SPEC.md decision 9). It sits over the whole view area rather
// than inside either file view so the two don't each need a copy -- and so an
// empty *folder* can reuse it later; v1 only speaks for the favourites listing,
// which is the one screen guaranteed to start empty.
ColumnLayout {
    id: root

    required property var navController

    readonly property var listModel: root.navController?.fileListModel ?? null

    visible: root.navController?.viewKind === ViewKind.Favourites && (root.listModel?.count ?? 0)
             === 0

    spacing: Theme.spacing.sm

    Label {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: Theme.color.textSecondary
        text: root.navController?.searchActive ? qsTr("No favourites match your search") : qsTr(
                                                     "No favourites yet")
    }

    Label {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: Theme.color.textSecondary
        visible: !(root.navController?.searchActive ?? false)
        // The menu row's own wording, not a second copy of it: the two would
        // drift apart the first time either is reworded.
        text: qsTr("Right-click a file or folder and choose \"%1\".").arg(
                  ActionCatalog.entries["toggleFavourite"].label({
                                                                     "favourited": false
                                                                 }))
    }
}
