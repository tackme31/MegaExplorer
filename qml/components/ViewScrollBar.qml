import QtQuick
import QtQuick.Controls

// FluentWinUI3 ships no ScrollBar, so the file views fall back to the Basic
// style's: thumb painted in palette.mid at 0.75 opacity, which on both schemes
// sits a shade off the surface behind it -- the bar is effectively invisible
// until dragged, when it switches to palette.dark. Restated here as three
// explicit steps so hover and plain wheel-scrolling are legible too.
ScrollBar {
    id: control

    // `hovered` drives both the colour below and the template's own active
    // state, and an attached bar never sets this itself.
    hoverEnabled: true

    contentItem: Rectangle {
        implicitWidth: control.interactive ? 6 : 2
        implicitHeight: control.interactive ? 6 : 2
        radius: width / 2
        color: control.pressed ? Theme.color.scrollThumbPressed : control.hovered
                                 ? Theme.color.scrollThumbHover : Theme.color.scrollThumb

        opacity: 0.0

        // Basic's own transient-bar machinery, kept verbatim apart from the
        // target opacity: the colours above already carry their alpha, so
        // dimming them a second time is what caused half the problem.
        states: State {
            name: "active"
            when: control.policy === ScrollBar.AlwaysOn || (control.active && control.size < 1.0)
            PropertyChanges {
                control.contentItem.opacity: 1.0
            }
        }

        transitions: Transition {
            from: "active"
            SequentialAnimation {
                PauseAnimation {
                    duration: 450
                }
                NumberAnimation {
                    target: control.contentItem
                    property: "opacity"
                    to: 0.0
                    duration: 200
                }
            }
        }
    }
}
