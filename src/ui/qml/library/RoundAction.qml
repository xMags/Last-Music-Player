import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// A collection header's round button (.collection-round, views.css 505-520):
// 40px, a faint border, the surface at 80% that turns solid on hover.
T.AbstractButton {
    id: control

    required property string iconName

    implicitWidth: 40
    implicitHeight: 40
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: control.iconName
            size: 18
            color: Theme.fg
        }
    }

    background: ButtonSurface {
        radius: 20
        color: control.hovered ? Theme.surface : Qt.alpha(Theme.surface, 0.8)
        border.width: 1
        border.color: Qt.alpha(Theme.fg, 0.12)
        focusShown: control.visualFocus
    }
}
