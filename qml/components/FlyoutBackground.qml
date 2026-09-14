import QtQuick
// Must be imported before any other QtQuick.Controls import (compile-time
// style selection per Qt docs' "Styling Qt Quick Controls"), same rule as
// Main.qml/Breadcrumb.qml/FileTableView.qml.
import QtQuick.Controls.FluentWinUI3
import QtQuick.Controls.FluentWinUI3.impl as Impl

// Background for a Menu or a ComboBox's drop-down. FluentWinUI3's
// popup-background.png has a noise grain in its 9-slice centre, and BorderImage
// stretches that centre over the whole popup, so the grain turns into visible
// blotches. The sprite still supplies the shadow and the 1px rim; the fill is flat.
Item {
    id: root

    // true where the popup's negative insets make the background cover the shadow
    // too (Menu); false where the shadow is drawn outside the bounds (ComboBox).
    property bool shadowWithinBounds: false

    readonly property var imageConfig: Config.controls.popup["normal"].background
    readonly property bool highContrast: Application.styleHints.accessibility.contrastPreference
                                         === Qt.HighContrast

    readonly property real panelLeft: shadowWithinBounds ? imageConfig.leftShadow : 0
    readonly property real panelTop: shadowWithinBounds ? imageConfig.topShadow : 0
    readonly property real panelWidth: width - (shadowWithinBounds ? imageConfig.leftShadow
                                                                     + imageConfig.rightShadow : 0)
    readonly property real panelHeight: height - (shadowWithinBounds ? imageConfig.topShadow
                                                                       + imageConfig.bottomShadow :
                                                                       0)

    implicitWidth: shadowWithinBounds ? 200 + imageConfig.leftShadow + imageConfig.rightShadow :
                                        imageConfig.width
    implicitHeight: shadowWithinBounds ? 30 + imageConfig.topShadow + imageConfig.bottomShadow :
                                         imageConfig.height

    Impl.StyleImage {
        anchors.fill: parent
        visible: !root.highContrast
        imageConfig: root.imageConfig
        drawShadowWithinBounds: root.shadowWithinBounds

        // Inset by the sprite's 1px rim, which stays visible around it.
        Rectangle {
            x: root.panelLeft + 1
            y: root.panelTop + 1
            width: root.panelWidth - 2
            height: root.panelHeight - 2
            radius: 7
            color: Theme.color.flyoutFill
        }
    }

    Rectangle {
        x: root.panelLeft
        y: root.panelTop
        width: root.panelWidth
        height: root.panelHeight
        visible: root.highContrast
        radius: 8
        color: root.palette.window
        border.color: root.palette.text
        border.width: 2
    }
}
