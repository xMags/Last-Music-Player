import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// The switch of Settings: 44 by 24, round. Off, a 1px grey border round the
// page colour with a grey knob at the left; on, the accent with a dark knob
// at the right.
T.AbstractButton {
    id: control

    implicitWidth: 44
    implicitHeight: 24
    checkable: true
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : Theme.disabledOpacity

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    background: Rectangle {
        implicitWidth: 44
        implicitHeight: 24
        radius: 12
        color: control.checked ? Theme.accentColor : SettingsStyle.field
        border.width: 1
        border.color: control.checked ? Theme.accentColor : SettingsStyle.off

        Behavior on color {
            enabled: !Motion.reduced
            ColorAnimation { duration: Motion.chipFade; easing: Motion.ease }
        }

        // 4px inside the border.
        Rectangle {
            x: control.checked ? 25 : 5
            y: 5
            width: 14
            height: 14
            radius: 7
            color: control.checked ? Theme.accentInk : Theme.fgSecondary

            Behavior on x {
                enabled: !Motion.reduced
                NumberAnimation { duration: Motion.chipFade; easing: Motion.ease }
            }
        }

        FocusRing {
            shown: control.visualFocus
            controlRadius: 12
        }
    }
}
