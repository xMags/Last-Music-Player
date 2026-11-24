import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import QtQuick.Templates as T
import LastMusicPlayer

// An artist page's Follow (.artist-follow, views.css 522-547;
// artist-follow.ts): a 40px outlined pill, padded 0 18px 0 14px, 14px 650 in
// the accent colour, with a person whose shoulder carries a round badge: a
// green plus to follow. Once followed it is a red Unfollow with a red minus.
// Waiting on the service it fades to 60%.
T.Button {
    id: control

    property bool followed: false
    property bool busy: false

    implicitHeight: 40
    implicitWidth: implicitContentWidth + leftPadding + rightPadding
    leftPadding: 14
    rightPadding: 18
    spacing: 8
    enabled: !busy
    opacity: enabled ? 1 : 0.6
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font: Theme.quickTitle
    text: followed ? qsTr("Unfollow") : qsTr("Follow")
    Accessible.checkable: true
    Accessible.checked: followed

    ToolTip.visible: hovered
    ToolTip.text: followed ? qsTr("Unfollow this artist") : qsTr("Follow this artist")
    ToolTip.delay: 500

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.BusyCursor
    }

    contentItem: Row {
        spacing: control.spacing

        // 21 by 19, from a 24 by 22 drawing.
        Item {
            anchors.verticalCenter: parent.verticalCenter
            width: 21
            height: 19

        Shape {
            id: glyph

            width: 24
            height: 22
            preferredRendererType: Shape.CurveRenderer
            transform: Scale { xScale: 21 / 24; yScale: 19 / 22 }

            ShapePath {
                strokeWidth: 0
                strokeColor: "transparent"
                fillColor: Theme.fg
                PathAngleArc { centerX: 9; centerY: 6; radiusX: 4.3; radiusY: 4.3; startAngle: 0; sweepAngle: 360 }
            }

            ShapePath {
                strokeWidth: 0
                strokeColor: "transparent"
                fillColor: Theme.fg
                PathSvg { path: "M1.2 20.5c0-4.6 3.5-7.9 7.8-7.9 2.1 0 4 .8 5.4 2.1-.5.9-.8 1.9-.8 3 0 1 .2 1.9.7 2.8z" }
            }

            // The badge, ringed in the surface colour.
            ShapePath {
                strokeWidth: 1.6
                strokeColor: Theme.surface
                fillColor: control.followed ? Theme.danger : "#22a04b"
                PathAngleArc { centerX: 18; centerY: 15.5; radiusX: 5.4; radiusY: 5.4; startAngle: 0; sweepAngle: 360 }
            }

            ShapePath {
                strokeWidth: 1.9
                strokeColor: "white"
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                PathSvg { path: control.followed ? "M15.4 15.5h5.2" : "M18 12.9v5.2M15.4 15.5h5.2" }
            }
        }
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            font: control.font
            color: control.followed ? Theme.danger : Theme.accentColor
        }
    }

    background: ButtonSurface {
        radius: height / 2
        border.width: 1
        border.color: control.followed ? Theme.mixSrgb(Theme.danger, "transparent", 0.45)
                                       : Theme.mixSrgb(Theme.fg, "transparent", 0.18)
        color: control.followed
            ? (control.hovered ? Theme.mixSrgb(Theme.danger, "transparent", 0.10) : "transparent")
            : (control.hovered ? Theme.surface : Theme.mixSrgb(Theme.surface, "transparent", 0.80))
        fadeDuration: Motion.controlFade
        fadeEasing: Motion.linear
        focusShown: control.visualFocus
    }
}
