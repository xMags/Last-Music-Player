import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// The account card at the foot of the sidebar (.profile, styles.css 369-405):
// a 40px avatar letter, the name and the plan label, and a chevron. Collapsed,
// only the avatar shows.
T.AbstractButton {
    id: control

    property string name
    property string plan
    property string letter
    property bool collapsed: false

    // padding: 20px 16px; collapsed: 16px 0
    topPadding: collapsed ? 16 : 20
    bottomPadding: collapsed ? 16 : 20
    leftPadding: collapsed ? 0 : 16
    rightPadding: collapsed ? 0 : 16
    implicitHeight: 40 + topPadding + bottomPadding
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: Item {
        Row {
            anchors.verticalCenter: parent.verticalCenter
            anchors.horizontalCenter: control.collapsed ? parent.horizontalCenter : undefined
            spacing: 12

            Rectangle {
                width: 40
                height: 40
                radius: 20
                color: Theme.neutral

                Text {
                    anchors.centerIn: parent
                    text: control.letter
                    font: Theme.profileName
                    color: Theme.fg
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                visible: !control.collapsed
                width: Math.max(0, control.availableWidth - 40 - 12 - dots.width - 12)

                Text {
                    width: parent.width
                    text: control.name
                    font: Theme.profileName
                    color: Theme.fg
                    elide: Text.ElideRight
                }

                Text {
                    topPadding: 2
                    text: control.plan
                    font: Theme.profileTier
                    color: Theme.accentColor
                }
            }
        }

        Text {
            id: dots

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: !control.collapsed
            text: "›"
            font: Theme.profileDots
            color: Theme.fgTertiary
        }
    }

    background: Rectangle {
        color: control.hovered ? Theme.hover : "transparent"

        // border-top: 1px solid divider
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.divider
        }

        FocusRing {
            shown: control.visualFocus
            offset: -2
        }
    }
}
