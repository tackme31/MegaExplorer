import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls
import QtQuick.Layouts

import MegaExplorer

// The advanced-search facets, opened from the funnel button beside the search
// field. The selections live here rather than in C++: the same argument the search
// field's own text makes -- the control owns what it shows, and the one event that
// can invalidate it (navigating, which drops the search) arrives as searchCleared.
//
// The four controls hold a *pending* edit and the applied* properties below hold
// what C++ is searching with; the only crossings are apply(), clearFilter() and
// revertPending().
// A ComboBox writes its own currentIndex when the user picks a row, and any write
// destroys a binding assigned to that property -- so the two sides are kept in step
// by assignment, and no binding is ever assigned to currentIndex/checked.
Popup {
    id: root

    // Set by whoever opens this.
    required property var navController

    // Exposed for tst_SearchFilterPopup.qml: the pending edit *is* these four
    // controls, so nothing else can drive a test.
    property alias typeSelector: typeBox
    property alias categorySelector: categoryBox
    property alias timeSelector: timeBox
    property alias favouriteSelector: favouriteCheck

    // What C++ is currently searching with. Dismissing the popup throws the pending
    // edit away and leaves these standing, so they cannot be bindings to the
    // controls.
    property int appliedType: SearchNodeType.Any
    property int appliedCategory: SearchCategory.Any
    property int appliedTime: SearchTimeWindow.Any
    property bool appliedFavourite: false

    readonly property bool filterActive: root.appliedType !== SearchNodeType.Any
                                         || root.appliedCategory !== SearchCategory.Any
                                         || root.appliedTime !== SearchTimeWindow.Any
                                         || root.appliedFavourite

    // Reads of currentIndex/checked, not bindings assigned to them, so these stay
    // live for the whole session -- see the note above.
    readonly property bool pendingActive: typeBox.currentIndex !== SearchNodeType.Any
                                          || categoryBox.currentIndex !== SearchCategory.Any
                                          || timeBox.currentIndex !== SearchTimeWindow.Any
                                          || favouriteCheck.checked

    readonly property bool dirty: typeBox.currentIndex !== root.appliedType
                                  || categoryBox.currentIndex !== root.appliedCategory
                                  || timeBox.currentIndex !== root.appliedTime
                                  || favouriteCheck.checked !== root.appliedFavourite

    // Same reasoning as the "More" menu's popupType in AddressToolBar.qml: on
    // Windows a Popup may resolve to a native window, and this one has to be able to
    // extend past the toolbar row it is anchored inside.
    popupType: Popup.Window

    padding: Theme.spacing.lg
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    // Every open starts from what is applied, which is what makes dismissing the
    // popup a discard: the abandoned edit cannot survive into the next open.
    onAboutToShow: root.revertPending()

    background: Rectangle {
        color: Theme.color.surfaceAlt
        border.color: Theme.color.stroke
        border.width: Theme.border.thin
        radius: Theme.radius.md
    }

    // Display only -- the caller has already cleared the C++ side (searchCleared),
    // so this deliberately does not push.
    function reset() {
        root.appliedType = SearchNodeType.Any;
        root.appliedCategory = SearchCategory.Any;
        root.appliedTime = SearchTimeWindow.Any;
        root.appliedFavourite = false;
        root.revertPending();
    }

    function revertPending() {
        typeBox.currentIndex = root.appliedType;
        categoryBox.currentIndex = root.appliedCategory;
        timeBox.currentIndex = root.appliedTime;
        favouriteCheck.checked = root.appliedFavourite;
    }

    // Clear commits, like Apply does: leaving it pending made the two buttons disagree
    // about when the results move, and "no filter" was then only reachable by clicking
    // Apply a second time.
    function clearFilter() {
        // apply()'s guard, and it has to come before the control writes: clearing them
        // without the mirror would disable this button (pendingActive) while the chip
        // still shows the filter as applied (filterActive), with no way back.
        if (!root.navController)
            return;
        typeBox.currentIndex = SearchNodeType.Any;
        categoryBox.currentIndex = SearchCategory.Any;
        timeBox.currentIndex = SearchTimeWindow.Any;
        favouriteCheck.checked = false;
        // Nothing applied means C++ already holds this empty filter, and re-pushing it
        // would re-run a search that blocks the GUI thread.
        if (!root.filterActive)
            return;
        root.appliedType = SearchNodeType.Any;
        root.appliedCategory = SearchCategory.Any;
        root.appliedTime = SearchTimeWindow.Any;
        root.appliedFavourite = false;
        root.navController.setSearchFilter(root.appliedType, root.appliedCategory, root.appliedTime,
                                           root.appliedFavourite);
    }

    // Every facet is pushed together, so C++ never sees a half-applied filter and only
    // re-runs the search once.
    function apply() {
        // navController is null only during the login/logout transition, and the
        // funnel button stays visible through it -- committing the mirror there would
        // light the chip for a filter C++ was never told about.
        if (!root.navController) {
            root.close();
            return;
        }
        root.appliedType = typeBox.currentIndex;
        root.appliedCategory = categoryBox.currentIndex;
        root.appliedTime = timeBox.currentIndex;
        root.appliedFavourite = favouriteCheck.checked;
        root.navController?.setSearchFilter(root.appliedType, root.appliedCategory, root.appliedTime,
                                            root.appliedFavourite);
        root.close();
    }

    ColumnLayout {
        spacing: Theme.spacing.md

        Label {
            text: qsTr("Type")
            color: Theme.color.textSecondary
            font.pixelSize: Theme.font.caption
        }

        ComboBox {
            id: typeBox

            Layout.fillWidth: true
            Layout.minimumWidth: 200
            // The recents listing is files-only by definition, so IMegaClient::listRecent
            // ignores this facet -- greyed out rather than left offering a choice with no
            // effect, the same bargain the category picker below makes.
            enabled: root.navController?.viewKind !== ViewKind.Recents
            // Index is the enum value: the model is listed in the enum's own order,
            // which SearchFilterEnums.h static_asserts against core's.
            model: [qsTr("Any"), qsTr("Files"), qsTr("Folders")]
            onActivated: index => {
                // A category other than "Any" makes the SDK return files only, so the
                // pair "folders + category" can only ever be empty -- drop the
                // category rather than let the user build a search with no answers.
                if (index === SearchNodeType.Folders)
                    categoryBox.currentIndex = SearchCategory.Any;
            }
        }

        Label {
            text: qsTr("Category")
            color: Theme.color.textSecondary
            font.pixelSize: Theme.font.caption
        }

        ComboBox {
            id: categoryBox

            Layout.fillWidth: true
            Layout.minimumWidth: 200
            // Greyed out rather than hidden, so "folders" visibly costs the category
            // instead of the row disappearing under the user's cursor. Not while the
            // type itself is disabled: that value is then ignored C++-side and cannot be
            // changed, so reading it here would grey both pickers out with no way back.
            enabled: !typeBox.enabled || typeBox.currentIndex !== SearchNodeType.Folders
            model: [qsTr("Any"), qsTr("Images"), qsTr("Audio"), qsTr("Video"), qsTr("Documents"),
                qsTr("PDFs"), qsTr("Presentations"), qsTr("Spreadsheets"), qsTr("Archives"), qsTr(
                    "Programs"), qsTr("Other")]
        }

        Label {
            text: qsTr("Created")
            color: Theme.color.textSecondary
            font.pixelSize: Theme.font.caption
        }

        ComboBox {
            id: timeBox

            Layout.fillWidth: true
            Layout.minimumWidth: 200
            // Rolling windows, not calendar days -- the labels say so because that is
            // literally what MegaSearchFilter is given (see core/SearchFilter.h).
            model: [qsTr("Any time"), qsTr("Past 24 hours"), qsTr("Past 7 days"), qsTr(
                    "Past 30 days"), qsTr("Past year")]
        }

        CheckBox {
            id: favouriteCheck

            // Every row of the favourites listing is already a favourite, so this
            // narrows nothing there.
            enabled: root.navController?.viewKind !== ViewKind.Favourites
            text: qsTr("Favourites only")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing.sm

            // Stays open afterwards: unlike Apply this is a "start over" step, and the
            // user is as likely to pick new facets as to leave.
            Button {
                Layout.fillWidth: true
                text: qsTr("Clear")
                enabled: root.pendingActive
                onClicked: root.clearFilter()
            }

            Button {
                Layout.fillWidth: true
                text: qsTr("Apply")
                highlighted: true
                // Disabled while the selections match what is applied: re-pushing an
                // identical filter would re-run a search that blocks the GUI thread.
                enabled: root.dirty
                onClicked: root.apply()
            }
        }
    }
}
