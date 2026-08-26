import QtQuick
// Before any other QtQuick.Controls import, same rule as its siblings.
import QtQuick.Controls.FluentWinUI3

// What a view shows between the click that asks for a favourites/recents listing
// and its rows arriving. It sits in the same slot as EmptyListingNotice and is
// mutually exclusive with it: the model is emptied at the click, so without this
// the screen would answer "No favourites yet" while the walk is still running.
// Wordless on purpose -- the spinner already says "wait", and a translated
// "loading" label would only repeat it.
//
// Gated on busy as well as listingPending so BusyState's spinner delay covers this
// too -- a listing that lands inside it goes past without flashing anything.
BusyIndicator {
    id: root

    required property var navController

    visible: (root.navController?.listingPending ?? false) && (root.navController?.busy ?? false)

    width: Theme.iconSize.lg
    height: Theme.iconSize.lg

    // Gated the way TabStrip.qml gates its own: whether the style stops
    // animating a hidden indicator is style-private, and a stuck animation
    // drives the render loop for as long as the tab lives.
    running: root.visible
}
