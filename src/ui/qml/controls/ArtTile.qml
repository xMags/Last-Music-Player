pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import LastMusicPlayer

// A cover: the picture at `source` with rounded corners, over a gradient tile
// (chosen from `artworkId`) with `label` in the middle, which is what shows
// while the picture loads or when it cannot load. When `source` changes, the
// old picture stays until the new one has loaded and fades in over it.
Item {
    id: root

    property url source
    // Picks the placeholder gradient; the same id always gets the same one.
    property string artworkId
    // The letter drawn on the placeholder (ArtworkPlaceholder::tileLabelFor).
    property string label
    // Large tiles (the full-size cover) draw a bigger letter.
    property bool big: false
    property real radius: Theme.radiusMd
    property bool shadow: false
    property var shadowLayers: Theme.shadowArt

    readonly property bool loaded: current.status === Image.Ready

    implicitWidth: 160
    implicitHeight: 160

    onSourceChanged: {
        previous.source = current.status === Image.Ready ? current.source : ""
        current.opacity = 0
        current.source = root.source
        revealTimeout.restart()
    }

    function reveal() {
        revealTimeout.stop()
        if (Motion.reduced || previous.source.toString() === "") {
            current.opacity = 1
            previous.source = ""
            return
        }
        fadeIn.restart()
    }

    BoxShadow {
        anchors.fill: parent
        visible: root.shadow
        layers: root.shadowLayers
        radius: root.radius
    }

    Item {
        id: content

        anchors.fill: parent
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: mask
        }

        // linear-gradient(135deg, a, b): corner to corner on a square.
        Shape {
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: "transparent"
                fillGradient: LinearGradient {
                    x1: root.width / 2 - (root.width + root.height) / 4
                    y1: root.height / 2 - (root.width + root.height) / 4
                    x2: root.width / 2 + (root.width + root.height) / 4
                    y2: root.height / 2 + (root.width + root.height) / 4
                    GradientStop { position: 0; color: Theme.artworkGradientStart(root.artworkId) }
                    GradientStop { position: 1; color: Theme.artworkGradientEnd(root.artworkId) }
                }
                PathRectangle { width: root.width; height: root.height }
            }
        }

        Text {
            anchors.centerIn: parent
            text: root.label
            color: Theme.artLabel
            font: root.big ? Theme.artTileLabelBig : Theme.artTileLabel
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: Theme.artLabelShadow
                shadowVerticalOffset: 2
                shadowHorizontalOffset: 0
                // drop-shadow blur 6px
                blurMax: 6
                shadowBlur: 1
            }
        }

        // The sheen: lighter at the top, a touch darker at the bottom.
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0; color: Theme.artSheenTop }
                GradientStop { position: 0.35; color: "transparent" }
                GradientStop { position: 1; color: Theme.artSheenBottom }
            }
        }

        Image {
            id: previous

            anchors.fill: parent
            asynchronous: true
            fillMode: Image.PreserveAspectCrop
            sourceSize: Qt.size(root.width * Screen.devicePixelRatio, root.height * Screen.devicePixelRatio)
        }

        Image {
            id: current

            anchors.fill: parent
            asynchronous: true
            fillMode: Image.PreserveAspectCrop
            sourceSize: Qt.size(root.width * Screen.devicePixelRatio, root.height * Screen.devicePixelRatio)
            opacity: 0

            onStatusChanged: {
                if (status === Image.Ready)
                    root.reveal()
                else if (status === Image.Error)
                    previous.source = ""
            }
        }
    }

    Rectangle {
        id: mask

        anchors.fill: parent
        radius: root.radius
        visible: false
        layer.enabled: true
    }

    NumberAnimation {
        id: fadeIn

        target: current
        property: "opacity"
        to: 1
        duration: Motion.artCrossfade
        easing: Motion.standard
        onFinished: previous.source = ""
    }

    // A slow picture should not keep the old one up for long.
    Timer {
        id: revealTimeout

        interval: Motion.artRevealTimeout
        onTriggered: previous.source = ""
    }

    Component.onCompleted: current.source = root.source
}
