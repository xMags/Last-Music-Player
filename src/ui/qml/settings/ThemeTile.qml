import QtQuick
import QtQuick.Shapes
import QtQuick.Templates as T
import LastMusicPlayer

// One theme choice (.settings-theme-tile, views.css 1293-1334): a 76px tile
// with a small drawing of the window in that theme (a sidebar strip, two grey
// lines and an accent line) over its label. Selected, the border turns to the
// accent and doubles. `mode` is "light", "dark" or "system".
T.AbstractButton {
    id: tile

    required property string mode
    property bool selected: false

    // The preview's fixed colours (views.css 1326-1334).
    readonly property color previewSidebar: mode === "light" ? "#EDEDF2" : (mode === "dark" ? "#1B1B1B" : Qt.rgba(120 / 255, 120 / 255, 130 / 255, 0.35))
    readonly property color previewLine: mode === "light" ? Qt.rgba(0, 0, 0, 0.22) : (mode === "dark" ? Qt.rgba(1, 1, 1, 0.3) : Qt.rgba(120 / 255, 120 / 255, 130 / 255, 0.75))

    implicitWidth: 76
    implicitHeight: 5 + 40 + 5 + label.implicitHeight + 6
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    background: Rectangle {
        radius: 9
        color: "transparent"
        border.width: tile.selected ? 2 : 1
        border.color: tile.selected ? Theme.accentColor : (tile.hovered ? Theme.fgTertiary : Theme.strokeStrong)

        Behavior on border.color {
            enabled: !Motion.reduced
            ColorAnimation { duration: Motion.chipFade; easing: Motion.ease }
        }

        FocusRing {
            shown: tile.visualFocus
            controlRadius: 9
        }
    }

    contentItem: Item {
        Rectangle {
            id: preview

            x: 5
            y: 5
            width: parent.width - 2 * 5
            height: 40
            radius: 5
            clip: true
            color: tile.mode === "dark" ? "#232323" : "#F7F7FA"

            // The system tile is light, then dark past a line through the
            // middle at 45 degrees (linear-gradient(135deg, light 50%, dark 50%)):
            // the dark side is where x + y passes the centre's x + y.
            Shape {
                anchors.fill: parent
                visible: tile.mode === "system"
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    id: darkSide

                    readonly property real split: (preview.width + preview.height) / 2

                    strokeColor: "transparent"
                    fillColor: "#232323"
                    startX: darkSide.split; startY: 0
                    PathLine { x: preview.width; y: 0 }
                    PathLine { x: preview.width; y: preview.height }
                    PathLine { x: darkSide.split - preview.height; y: preview.height }
                }
            }

            Rectangle {
                width: 16
                height: parent.height
                color: tile.previewSidebar
            }

            Column {
                x: 16 + 5
                y: 6
                width: parent.width - x
                spacing: 4

                Rectangle { width: parent.width * 0.7; height: 4; radius: 2; color: tile.previewLine }
                Rectangle { width: parent.width * 0.45; height: 4; radius: 2; color: tile.previewLine }
                Rectangle { width: parent.width * 0.55; height: 4; radius: 2; color: Theme.accentFill }
            }
        }

        Text {
            id: label

            anchors.horizontalCenter: parent.horizontalCenter
            y: preview.y + preview.height + 5
            text: tile.text
            font: Theme.settingsTileLabel
            color: tile.selected ? Theme.fg : Theme.fgSecondary
        }
    }
}
