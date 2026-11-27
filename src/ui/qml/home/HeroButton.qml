import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import LastMusicPlayer

// The hero card's buttons (styles.css 2463-2509), all 40px high:
// - Play (.home-hero-play): an accent pill with the accent ink, a 16px icon,
//   padded 0 20px 0 16px, 14px 700; brighter on hover, 97% while pressed.
// - Radio (.home-hero-radio): an outlined pill on the page colour at 30%
//   (55% on hover), an 18px icon, padded 0 18px 0 14px, 14px 650.
// - More (.home-hero-more): a 40px circle in the same frosted fill, an 18px
//   icon.
T.Button {
    id: control

    enum Kind { Play, Radio, More }

    property int kind: HeroButton.Play
    property string iconName: ""
    // The reference's title attribute.
    property string toolTip: ""

    readonly property bool round: kind === HeroButton.More
    readonly property bool play: kind === HeroButton.Play

    implicitHeight: 40
    implicitWidth: round ? 40 : implicitContentWidth + leftPadding + rightPadding
    leftPadding: round ? 0 : play ? 16 : 14
    rightPadding: round ? 0 : play ? 20 : 18
    spacing: 8
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font: play ? Theme.collectionPlay : Theme.quickTitle
    scale: play && down && !Motion.reduced ? 0.97 : 1

    ToolTip.visible: toolTip !== "" && hovered
    ToolTip.text: toolTip
    ToolTip.delay: 500

    Behavior on scale {
        enabled: !Motion.reduced
        NumberAnimation { duration: Motion.controlFade; easing: Motion.standard }
    }

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: Item {
        implicitWidth: control.round ? 40 : row.implicitWidth
        implicitHeight: 40

        Row {
            id: row

            anchors.centerIn: parent
            spacing: control.spacing

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: control.iconName
                size: control.play ? 16 : 18
                color: label.color
            }

            AppText {
                id: label

                anchors.verticalCenter: parent.verticalCenter
                visible: !control.round
                text: control.text
                font: control.font
                color: control.play ? Theme.accentInk : Theme.fg
            }
        }
    }

    background: ButtonSurface {
        radius: height / 2
        border.width: control.play ? 0 : 1
        border.color: Theme.strokeStrong
        color: control.play
            ? (control.hovered ? Theme.brightness(Theme.accentColor, 1.06) : Theme.accentColor)
            : Theme.mixSrgb(Theme.bg, "transparent", control.hovered ? 0.55 : 0.30)
        fadeDuration: control.play ? Motion.controlFade : 0
        focusShown: control.visualFocus
    }
}
