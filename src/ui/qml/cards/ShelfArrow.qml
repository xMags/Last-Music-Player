import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// A shelf's arrow (.row-nav, styles.css 1075-1097): a 36px round surface
// button with a soft shadow; it fades in while the shelf is hovered, takes
// no clicks while hidden, and shrinks a little when pressed.
T.AbstractButton {
    id: arrow

    property string iconName: "chevron"
    property bool pointsBack: false
    property bool shown: false

    implicitWidth: 36
    implicitHeight: 36
    z: 6
    opacity: shown || visualFocus ? 1 : 0
    enabled: opacity > 0
    scale: down ? 0.92 : 1
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: pointsBack ? qsTr("Previous") : qsTr("Next")

    Behavior on opacity {
        enabled: !Motion.reduced
        NumberAnimation { duration: Motion.chipFade }
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    background: Item {
        BoxShadow {
            anchors.fill: parent
            radius: width / 2
            layers: Theme.shadowFlyout
        }

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: arrow.hovered ? Theme.surfaceAlt : Theme.surface
        }
    }

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: arrow.iconName
            size: 16
            color: Theme.fg
            rotation: arrow.pointsBack ? 180 : 0
        }
    }
}
