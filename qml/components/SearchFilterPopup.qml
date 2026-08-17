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
// The four controls are themselves the state; there is deliberately no mirror of
// them in properties up here. A ComboBox writes its own currentIndex when the user
// picks a row, and any write destroys a binding assigned to that property -- so a
// `currentIndex: root.someMirror` binding would be dead from the first pick, and
// reset() would then clear the filter while the boxes went on showing the old one.
Popup {
    id: root

    // Set by whoever opens this.
    required property var navController

    // Reads of currentIndex/checked, not bindings assigned to them, so this stays
    // live for the whole session -- see the note above.
    readonly property bool filterActive: typeBox.currentIndex !== SearchNodeType.Any
                                         || categoryBox.currentIndex !== SearchCategory.Any
                                         || timeBox.currentIndex !== SearchTimeWindow.Any
                                         || favouriteCheck.checked

    // Same reasoning as the "More" menu's popupType in AddressToolBar.qml: on
    // Windows a Popup may resolve to a native window, and this one has to be able to
    // extend past the toolbar row it is anchored inside.
    popupType: Popup.Window

    padding: Theme.spacing.lg
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    background: Rectangle {
        color: Theme.color.surfaceAlt
        border.color: Theme.color.stroke
        border.width: Theme.border.thin
        radius: Theme.radius.md
    }

    // Display only -- the caller either already cleared the C++ side (searchCleared)
    // or calls apply() itself right after, so this deliberately does not push.
    function reset() {
        typeBox.currentIndex = SearchNodeType.Any;
        categoryBox.currentIndex = SearchCategory.Any;
        timeBox.currentIndex = SearchTimeWindow.Any;
        favouriteCheck.checked = false;
    }

    // One entry point for all four controls: every facet is pushed together, so C++
    // never sees a half-applied filter and only re-runs the search once per change.
    function apply() {
        root.navController?.setSearchFilter(typeBox.currentIndex, categoryBox.currentIndex,
                                            timeBox.currentIndex, favouriteCheck.checked);
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
            // Index is the enum value: the model is listed in the enum's own order,
            // which SearchFilterEnums.h static_asserts against core's.
            model: [qsTr("Any"), qsTr("Files"), qsTr("Folders")]
            onActivated: index => {
                // A category other than "Any" makes the SDK return files only, so the
                // pair "folders + category" can only ever be empty -- drop the
                // category rather than let the user build a search with no answers.
                if (index === SearchNodeType.Folders)
                    categoryBox.currentIndex = SearchCategory.Any;
                root.apply();
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
            // instead of the row disappearing under the user's cursor.
            enabled: typeBox.currentIndex !== SearchNodeType.Folders
            model: [qsTr("Any"), qsTr("Images"), qsTr("Audio"), qsTr("Video"), qsTr("Documents"),
                qsTr("PDFs"), qsTr("Presentations"), qsTr("Spreadsheets"), qsTr("Archives"), qsTr(
                    "Programs"), qsTr("Other")]
            onActivated: root.apply()
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
            onActivated: root.apply()
        }

        CheckBox {
            id: favouriteCheck

            text: qsTr("Favourites only")
            onToggled: root.apply()
        }

        Button {
            Layout.fillWidth: true
            text: qsTr("Clear filters")
            enabled: root.filterActive
            onClicked: {
                root.reset();
                root.apply();
            }
        }
    }
}
