pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import LastMusicPlayer

// A genre's drawn cover, from the current app (GenreArtworkTile.cpp): the
// genre's diagonal gradient with a soft highlight in the top-left corner, the
// stripe overlay and a shade at the bottom, a faint enlarged copy of the mark
// in the lower right, the mark itself in the middle, a thin light edge and
// the "genre" label with its dot in the lower left. The marks come from the
// genre's recipe (core GenreArtworkRecipe), drawn in a 96-unit box. The
// stripe overlay and the enlarged mark reach the corners, so the drawing is
// cut to `radius`; the label stays outside that layer, where text is sharp.
Item {
    id: tile

    property string genre: ""
    property real radius: 16
    readonly property var recipe: GenreArtwork.recipe(genre)
    readonly property color ink: Qt.rgba(1, 1, 1, 0xEB / 255)

    RoundedClip {
        anchors.fill: parent
        radius: tile.radius

        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: "transparent"
                fillGradient: LinearGradient {
                    x1: 0
                    y1: 0
                    x2: tile.width
                    y2: tile.height
                    GradientStop { position: 0; color: tile.recipe.start }
                    GradientStop { position: 1; color: tile.recipe.end }
                }
                PathRectangle {
                    width: tile.width
                    height: tile.height
                    radius: tile.radius
                }
            }
        }

        // The highlight in the top-left corner.
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: "transparent"
                fillGradient: RadialGradient {
                    centerX: tile.width * 0.12
                    centerY: tile.height * 0.06
                    centerRadius: tile.width * 1.05
                    focalX: centerX
                    focalY: centerY
                    GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0x57 / 255) }
                    GradientStop { position: 0.62; color: Qt.rgba(1, 1, 1, 0) }
                }
                PathRectangle {
                    width: tile.width
                    height: tile.height
                    radius: tile.radius
                }
            }
        }

        Image {
            anchors.fill: parent
            source: "qrc:/qt/qml/LastMusicPlayer/library/LibraryStripeOverlay.svg"
            fillMode: Image.Stretch
            sourceSize.width: 256
            sourceSize.height: 256
        }

        // The shade along the bottom.
        Rectangle {
            anchors.fill: parent
            radius: tile.radius
            gradient: Gradient {
                GradientStop { position: 0.54; color: Qt.rgba(0, 0, 0, 0) }
                GradientStop { position: 1; color: Qt.rgba(0, 0, 0, 0x33 / 255) }
            }
        }

        // A faint copy of the mark, twice as large, in the lower right.
        GenreMark {
            x: tile.width - 96 + 52
            y: tile.height - 96 + 46
            marks: tile.recipe.marks
            ink: tile.ink
            opacity: 0.16
            scale: 2.1
            transformOrigin: Item.BottomRight
        }

        GenreMark {
            anchors.centerIn: parent
            anchors.verticalCenterOffset: -8 * tile.width / 160
            marks: tile.recipe.marks
            ink: tile.ink
            scale: tile.width / 160
        }

        Rectangle {
            anchors.fill: parent
            radius: tile.radius
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0x38 / 255)
        }
    }

    Row {
        x: 18 * tile.width / 160
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 16 * tile.width / 160
        spacing: 8
        visible: tile.width >= 100

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 5
            height: 5
            radius: 2.5
            color: Qt.rgba(1, 1, 1, 0xD9 / 255)
        }

        Text {
            text: qsTr("genre")
            font: Theme.genreLabel
            color: tile.ink
        }
    }
}
