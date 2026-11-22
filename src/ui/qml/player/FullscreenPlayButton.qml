import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// The play button of the full-screen player (.fs-play, styles.css 2195-2207):
// a 68px white disc with a dark 28px glyph and a soft shadow, growing to
// 105% under the pointer (0.1s ease).
T.Button {
    id: control

    property bool playing: false

    implicitWidth: 68
    implicitHeight: 68
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    scale: hovered ? 1.05 : 1

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
            size: 28
            color: Theme.fullscreenPlayGlyph
        }
    }

    background: Item {
        BoxShadow {
            anchors.fill: parent
            layers: Theme.shadowFullscreenPlay
            radius: width / 2
        }

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Theme.fullscreenPlayFill
        }

        FocusRing {
            shown: control.visualFocus
            controlRadius: width / 2
        }
    }
}
