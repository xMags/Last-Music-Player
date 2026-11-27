import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// One accent colour of Settings: a 32px dot; the chosen one has a 2px ring
// in the text colour with the page colour between ring and dot.
T.AbstractButton {
    id: swatch

    required property color seed
    property bool selected: false

    implicitWidth: 32
    implicitHeight: 32
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    background: Rectangle {
        radius: 16
        color: swatch.seed
        border.width: 2
        border.color: swatch.selected ? SettingsStyle.title : "transparent"

        Rectangle {
            anchors.fill: parent
            anchors.margins: 2
            radius: width / 2
            visible: swatch.selected
            color: "transparent"
            border.width: 2
            border.color: SettingsStyle.field
        }

        FocusRing {
            shown: swatch.visualFocus
            controlRadius: 16
        }
    }
}
