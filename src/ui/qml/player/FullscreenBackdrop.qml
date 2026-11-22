import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import LastMusicPlayer

// The full-screen player's backdrop (.fs-bg, styles.css 2112-2120): the
// cover, 60px larger than the player on every side and scaled to 110%,
// heavily blurred, more saturated and darkened to 42% brightness. Without a
// cover (or until it loads) a diagonal gradient from the accent fill to deep
// blue stands in, darkened the same way.
Item {
    id: root

    property url source

    // linear-gradient(135deg, accent fill, #1a2340): corner to corner.
    Shape {
        anchors.fill: parent
        visible: cover.status !== Image.Ready
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: root.width / 2 - (root.width + root.height) / 4
                y1: root.height / 2 - (root.width + root.height) / 4
                x2: root.width / 2 + (root.width + root.height) / 4
                y2: root.height / 2 + (root.width + root.height) / 4
                GradientStop { position: 0; color: Theme.accentFill }
                GradientStop { position: 1; color: Theme.fullscreenBackdropEnd }
            }
            PathRectangle { width: root.width; height: root.height }
        }
    }

    Item {
        anchors.fill: parent
        anchors.margins: -Theme.fullscreenBackdropInset
        scale: Theme.fullscreenBackdropScale
        visible: cover.status === Image.Ready

        Image {
            id: cover

            anchors.fill: parent
            source: root.source
            asynchronous: true
            fillMode: Image.PreserveAspectCrop
            // Blurred to a wash, so a small decode is enough.
            sourceSize: Qt.size(256, 256)
            visible: false
        }

        MultiEffect {
            anchors.fill: cover
            source: cover
            blurEnabled: true
            blurMax: Theme.fullscreenBackdropBlur
            blur: 1
            saturation: Theme.fullscreenBackdropSaturation - 1
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.fullscreenBackdropDim
    }
}
