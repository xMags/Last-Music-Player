import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// One theme choice: a tile padded 10px with 10px corners, a 64px preview of
// the window in that theme (light, dark, or half of each for System) over
// its 13px label, 10px apart. Selected, the border turns to the accent.
// `mode` is "light", "dark" or "system".
T.AbstractButton {
    id: tile

    required property string mode
    property bool selected: false

    implicitWidth: 140
    implicitHeight: 10 + 64 + 10 + label.implicitHeight + 10
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.RadioButton
    Accessible.checked: tile.selected

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    background: Rectangle {
        radius: 10
        color: SettingsStyle.tile
        border.width: 1
        border.color: tile.selected ? Theme.accentColor : (tile.hovered ? SettingsStyle.quietHover : SettingsStyle.quiet)

        Behavior on border.color {
            enabled: !Motion.reduced
            ColorAnimation { duration: Motion.chipFade; easing: Motion.ease }
        }

        FocusRing {
            shown: tile.visualFocus
            controlRadius: 10
        }
    }

    contentItem: Item {
        Rectangle {
            id: preview

            x: 10
            y: 10
            width: parent.width - 2 * 10
            height: 64
            radius: 6
            color: tile.mode === "dark" ? "#202020" : "#F3F3F3"
            border.width: 1
            border.color: tile.mode === "light" ? "#DDDDDD" : "#3A3A3A"

            // System: the right half dark (linear-gradient(90deg, light 50%,
            // dark 50%)), inside the border and its right corners.
            Rectangle {
                visible: tile.mode === "system"
                x: parent.width / 2
                y: 1
                width: parent.width / 2 - 1
                height: parent.height - 2
                color: "#202020"
                topRightRadius: preview.radius - 1
                bottomRightRadius: preview.radius - 1
            }
        }

        Text {
            id: label

            x: 10
            y: preview.y + preview.height + 10
            text: tile.text
            font.family: SettingsStyle.family
            font.pixelSize: 13
            color: SettingsStyle.title
        }
    }
}
