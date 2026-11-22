import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// The settings switch (.settings-switch, views.css 1214-1241): a 40 x 20 pill
// with a 12px knob. Off is an outline with a small grey knob; on is filled
// with the accent and the knob slides 20px right. Hover grows the knob.
T.Switch {
    id: control

    implicitWidth: 40
    implicitHeight: 20
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : Theme.disabledSwitchOpacity

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    indicator: Rectangle {
        id: track

        implicitWidth: 40
        implicitHeight: 20
        radius: height / 2
        color: control.checked ? Theme.accentColor : "transparent"
        border.width: 1
        border.color: control.checked ? "transparent" : Theme.fgSecondary

        Behavior on color {
            enabled: !Motion.reduced
            ColorAnimation { duration: Motion.switchToggle; easing: Motion.ease }
        }
        Behavior on border.color {
            enabled: !Motion.reduced
            ColorAnimation { duration: Motion.switchToggle; easing: Motion.ease }
        }

        Rectangle {
            id: knob

            readonly property real restScale: control.hovered ? 1 : 0.84

            // 1px border plus 3px padding, then the 20px slide when on.
            x: control.checked ? 24 : 4
            anchors.verticalCenter: parent.verticalCenter
            width: 12
            height: 12
            radius: width / 2
            color: control.checked ? Theme.accentInk : Theme.fgSecondary
            scale: control.checked ? (control.hovered ? 1.1 : 1) : restScale

            Behavior on x {
                enabled: !Motion.reduced
                NumberAnimation { duration: Motion.switchToggle; easing: Motion.standard }
            }
            Behavior on scale {
                enabled: !Motion.reduced
                NumberAnimation { duration: Motion.switchToggle; easing: Motion.standard }
            }
            Behavior on color {
                enabled: !Motion.reduced
                ColorAnimation { duration: Motion.switchToggle; easing: Motion.ease }
            }
        }

        FocusRing {
            shown: control.visualFocus
            controlRadius: track.radius
        }
    }

    contentItem: Item {}
}
