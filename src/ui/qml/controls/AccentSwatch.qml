import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// One accent colour choice (.settings-accent, views.css 1338-1360): a 22px
// disc of the colour inside a 30px ring that darkens when selected. The disc
// grows a little on hover.
T.AbstractButton {
    id: control

    required property color seed
    property bool selected: false

    implicitWidth: 30
    implicitHeight: 30
    padding: 0
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    background: Rectangle {
        radius: width / 2
        color: "transparent"
        border.width: 2
        border.color: control.selected ? Theme.mixSrgb(Theme.fg, "transparent", 0.35) : "transparent"

        Behavior on border.color {
            enabled: !Motion.reduced
            ColorAnimation { duration: Motion.chipFade; easing: Motion.ease }
        }

        FocusRing {
            shown: control.visualFocus
            controlRadius: width / 2
        }
    }

    contentItem: Item {
        Rectangle {
            anchors.centerIn: parent
            width: 22
            height: 22
            radius: width / 2
            color: control.seed
            border.width: 1
            border.color: Theme.swatchRing
            scale: control.hovered ? 1.08 : 1

            Behavior on scale {
                enabled: !Motion.reduced
                NumberAnimation { duration: Motion.chipFade; easing: Motion.standard }
            }
        }
    }
}
