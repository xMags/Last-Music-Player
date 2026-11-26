pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// homeWelcomeMarkup: For you before the listener has played or liked
// anything. The hero's field lines behind, so it reads as part of Home;
// an eyebrow, the title, a sentence, and Browse charts and Search
// (.home-welcome-*, styles.css 2866-2922).
Item {
    id: welcome

    // Search was pressed: the page opens Browse with an empty search.
    signal searchRequested()

    implicitHeight: Math.max(216, copy.implicitHeight + 2 * 28)

    Rectangle {
        id: welcomeBox

        anchors.fill: parent
        radius: 16
        color: Theme.mixSrgb(Theme.surface, "transparent", 0.82)
        border.width: 1
        border.color: Theme.stroke
        clip: true

        HeroField {
            anchors.fill: parent
            anchors.margins: 1
            running: welcome.visible
        }

        // radial-gradient(120% 140% at 100% 50%, accent 16%, transparent 60%)
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: 15
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.4; color: Qt.alpha(Theme.accentColor, 0) }
                GradientStop { position: 1; color: Qt.alpha(Theme.accentColor, 0.16) }
            }
        }
    }

    Column {
        id: copy

        x: 32
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(520, parent.width - 2 * 32)
        spacing: 10

        Row {
            spacing: 6

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "sparkle"
                size: 14
                color: Theme.accentColor
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Getting started").toUpperCase()
                font: Theme.welcomeEyebrow
                color: Theme.accentColor
            }
        }

        Text {
            width: parent.width
            text: qsTr("Your music starts here")
            font: Theme.welcomeTitle
            lineHeight: Theme.lineHeightWelcomeTitle
            lineHeightMode: Text.FixedHeight
            color: Theme.fg
            wrapMode: Text.Wrap
        }

        Text {
            width: parent.width
            text: qsTr("Play or like a few songs. Last Music turns them into mixes made for you, and keeps your recent songs here.")
            font: Theme.welcomeText
            lineHeight: Theme.lineHeightWelcomeText
            lineHeightMode: Text.FixedHeight
            color: Theme.fgSecondary
            wrapMode: Text.Wrap
        }

        // .home-welcome-actions: 10px apart, 6px more above.
        Flow {
            width: parent.width
            topPadding: 6
            spacing: 10

            WelcomeButton {
                primary: true
                iconName: "flame"
                text: qsTr("Browse charts")
                onClicked: App.navigator.openView(Route.Browse)
            }

            WelcomeButton {
                iconName: "browse"
                text: qsTr("Search")
                onClicked: welcome.searchRequested()
            }
        }
    }

    // .home-welcome-button: a 40px pill padded 0 18px, an icon 8px before
    // 14px 600 text; the accent fill for the first. Shrinks to 98% pressed.
    component WelcomeButton: T.Button {
        id: welcomeButton

        property bool primary: false
        property string iconName: ""

        implicitHeight: 40
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        leftPadding: 18
        rightPadding: 18
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        scale: down && !Motion.reduced ? 0.98 : 1

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }

        contentItem: Row {
            spacing: 8

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: welcomeButton.iconName
                size: 15
                color: welcomeButton.primary ? Theme.accentInk : Theme.fg
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: welcomeButton.text
                font: Theme.welcomeButton
                color: welcomeButton.primary ? Theme.accentInk : Theme.fg
            }
        }

        background: ButtonSurface {
            radius: height / 2
            border.width: welcomeButton.primary ? 0 : 1
            border.color: Theme.stroke
            // .primary:hover is brightness(1.06).
            color: welcomeButton.primary ? (welcomeButton.hovered ? Theme.brightness(Theme.accentColor, 1.06) : Theme.accentColor)
                                         : (welcomeButton.hovered ? Theme.hover : Theme.surface)
            focusShown: welcomeButton.visualFocus
        }
    }
}
