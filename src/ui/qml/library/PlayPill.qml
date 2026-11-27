import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// A collection header's Play (.collection-play, views.css 487-504): a 40px
// accent pill with the play glyph and "Play", lifted 1px on hover.
T.AbstractButton {
    id: control

    implicitWidth: row.implicitWidth + 18 + 22
    implicitHeight: 40
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    text: qsTr("Play")
    transform: Translate { y: control.hovered && !control.down ? -1 : 0 }

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    contentItem: Item {
        Row {
            id: row

            x: 18 - control.leftPadding
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "play"
                size: 14
                color: "white"
            }

            AppText {
                text: control.text
                font: Theme.collectionPlay
                color: "white"
            }
        }
    }

    background: Item {
        BoxShadow {
            anchors.fill: parent
            layers: control.hovered ? Theme.shadowButtonHover : Theme.shadowButton
            radius: 20
        }

        ButtonSurface {
            anchors.fill: parent
            radius: 20
            color: Theme.accentFill
            focusShown: control.visualFocus
        }
    }
}
