pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Templates as T
import LastMusicPlayer

// The account card at the foot of the sidebar (.profile, styles.css 369-405):
// a 40px avatar (the picture when there is one, else the initials), the name
// and the plan label, and a chevron. Collapsed, only the avatar shows.
T.AbstractButton {
    id: control

    property string name
    property string plan
    property string letter
    // Already checked against the account address rules; empty for none.
    property url avatarUrl
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

                AppText {

                    onGlass: App.windowChrome.backdropActive
                    anchors.centerIn: parent
                    visible: picture.status !== Image.Ready
                    text: control.letter
                    font: Theme.profileName
                    color: Theme.fg
                }

                // Cut to the circle; the initials stay until it has loaded.
                Image {
                    id: picture

                    anchors.fill: parent
                    visible: status === Image.Ready
                    source: control.avatarUrl
                    sourceSize.width: 80
                    sourceSize.height: 80
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    layer.enabled: visible
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: circle
                        maskThresholdMin: 0.5
                        maskSpreadAtMin: 1.0
                    }
                }

                Rectangle {
                    id: circle

                    anchors.fill: parent
                    radius: width / 2
                    visible: false
                    layer.enabled: true
                }
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                visible: !control.collapsed
                width: Math.max(0, control.availableWidth - 40 - 12 - dots.width - 12)

                AppText {

                    onGlass: App.windowChrome.backdropActive
                    width: parent.width
                    text: control.name
                    font: Theme.profileName
                    color: Theme.fg
                    elide: Text.ElideRight
                }

                AppText {

                    onGlass: App.windowChrome.backdropActive
                    topPadding: 2
                    visible: control.plan !== ""
                    text: control.plan
                    font: Theme.profileTier
                    color: Theme.accentColor
                }
            }
        }

        AppText {

            onGlass: App.windowChrome.backdropActive
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
