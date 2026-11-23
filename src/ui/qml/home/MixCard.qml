import QtQuick
import QtQuick.Shapes
import QtQuick.Templates as T
import LastMusicPlayer

// A generated mix on Home (.playlist-card.home-mix-card, styles.css 792-870):
// a 184px square of the mix's diagonal gradient with a light sheen at the
// top, the badge in the top-left, its line at the bottom and a large faint
// sparkle in the top-right corner; the "..." button shows on hover. A click
// plays the mix; an empty mix is faded and does nothing.
T.AbstractButton {
    id: card

    required property string mixId
    required property string title
    required property string badge
    required property string description
    required property color colorStart
    required property color colorEnd
    required property bool empty

    signal menuRequested(point at)

    implicitWidth: 184
    implicitHeight: 184
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: empty ? 0.72 : 1
    Accessible.name: qsTr("Play %1").arg(title)

    Keys.onMenuPressed: card.menuRequested(Qt.point(width / 2, height / 2))

    HoverHandler { cursorShape: card.empty ? Qt.ArrowCursor : Qt.PointingHandCursor }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: eventPoint => card.menuRequested(eventPoint.position)
    }

    background: Item {
        BoxShadow {
            anchors.fill: parent
            layers: Theme.shadowCard
            radius: Theme.radiusXl
        }

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: "transparent"
                fillGradient: LinearGradient {
                    x1: 0
                    y1: 0
                    x2: card.width
                    y2: card.height
                    GradientStop { position: 0; color: card.colorStart }
                    GradientStop { position: 1; color: card.colorEnd }
                }
                PathRectangle {
                    width: card.width
                    height: card.height
                    radius: Theme.radiusXl
                }
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusXl
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.10) }
                GradientStop { position: 0.4; color: "transparent" }
            }
        }

        Item {
            anchors.fill: parent
            clip: true

            Icon {
                x: parent.width - 90 + 8
                y: -8
                name: "sparkle"
                size: 90
                color: "white"
                opacity: 0.18
            }
        }

        FocusRing {
            shown: card.visualFocus
        }
    }

    contentItem: Item {
        Text {
            x: 14
            y: 14
            width: parent.width - 28 - 30
            text: card.badge
            font: Theme.mixBadge
            color: "white"
            opacity: 0.95
            elide: Text.ElideRight
        }

        Text {
            x: 14
            width: parent.width - 28
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 14
            text: card.description
            font: Theme.mixBlurb
            lineHeight: Theme.lineHeightMixBlurb
            lineHeightMode: Text.FixedHeight
            color: "white"
            opacity: 0.94
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
        }

        IconButton {
            id: more

            x: parent.width - width - 8
            y: 8
            implicitWidth: 30
            implicitHeight: 30
            shape: IconButton.Overlay
            iconName: "more"
            iconSize: 15
            opacity: card.hovered || card.visualFocus ? 1 : 0
            Accessible.name: qsTr("More options for %1").arg(card.title)
            onClicked: card.menuRequested(more.mapToItem(card, 0, more.height + 4))
        }
    }
}
