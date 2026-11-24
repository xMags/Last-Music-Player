import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import LastMusicPlayer

// A collection header's round button (.collection-round, views.css 505-520):
// 40px, a faint border, the surface at 80% that turns solid on hover.
T.AbstractButton {
    id: control

    required property string iconName
    // The accent colour (a saved album, a finished download).
    property bool accent: false
    // The reference's title attribute, shown as a tooltip.
    property string toolTip: ""

    ToolTip.visible: toolTip !== "" && hovered
    ToolTip.text: toolTip
    ToolTip.delay: 500
    opacity: enabled ? 1 : Theme.disabledOpacity

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
            color: control.accent ? Theme.accentColor : Theme.fg
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
