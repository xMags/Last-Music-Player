import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// The filled round play button of the player bar (.pb-play, styles.css
// 1417-1431): 40px, the theme's play colours and a soft shadow. It grows a
// little on hover and shrinks while pressed.
T.Button {
    id: control

    property bool playing: false

    implicitWidth: 40
    implicitHeight: 40
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : Theme.disabledOpacity
    scale: !enabled ? 1 : down ? 0.96 : hovered ? 1.04 : 1

    Behavior on scale {
        enabled: !Motion.reduced
        NumberAnimation { duration: Motion.playPress; easing: Motion.ease }
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: control.playing ? "pause" : "play"
            size: 20
            color: Theme.playFg
        }
    }

    background: Item {
        implicitWidth: 40
        implicitHeight: 40

        BoxShadow {
            anchors.fill: parent
            layers: Theme.shadowPlayButton
            radius: width / 2
        }

        ButtonSurface {
            anchors.fill: parent
            radius: width / 2
            color: Theme.playBg
            border.width: 0
            rippleEnabled: control.enabled
            focusShown: control.visualFocus
        }
    }
}
