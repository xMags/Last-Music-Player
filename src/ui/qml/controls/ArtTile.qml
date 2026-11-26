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
    // Pictures are decoded at the tile's size in pixels. A zero size means
    // "full resolution" to an image, so the size is only known while the tile
    // is laid out and in a window (Screen.devicePixelRatio is 0 outside one):
    // nothing loads before that, and the last size is kept after it, so a
    // tile being torn down does not load its picture again at full size.
    readonly property bool sized: root.width > 0 && root.height > 0 && Screen.devicePixelRatio > 0
    property size decodeSize

    Binding on decodeSize {
        when: root.sized
        value: Qt.size(Math.ceil(root.width * Screen.devicePixelRatio), Math.ceil(root.height * Screen.devicePixelRatio))
        restoreMode: Binding.RestoreNone
    }

    // One picture filling the tile like Image.PreserveAspectCrop, with the
    // tile's rounded corners (shaders/roundedcover.frag).
    component CoverPicture: ShaderEffect {
        required property Image picture

        readonly property real itemAspect: width > 0 && height > 0 ? width / height : 1
        readonly property real pictureAspect: picture.implicitWidth > 0 && picture.implicitHeight > 0
            ? picture.implicitWidth / picture.implicitHeight : itemAspect

        property Image source: picture
        property size itemSize: Qt.size(width, height)
        property real cornerRadius: Math.min(root.radius, width / 2, height / 2)
        property size cropScale: pictureAspect > itemAspect ? Qt.size(itemAspect / pictureAspect, 1)
                                                            : Qt.size(1, pictureAspect / itemAspect)

        anchors.fill: parent
        visible: picture.status === Image.Ready && picture.opacity > 0
        opacity: picture.opacity
        fragmentShader: "qrc:/qt/qml/LastMusicPlayer/shaders/roundedcover.frag.qsb"
    }

    implicitWidth: 160
    implicitHeight: 160

    onSourceChanged: root.load()
    onDecodeSizeChanged: root.load()

    // The only place the images get a source or a size, always the size
    // first: a bound size can lag behind the source and load the picture at
    // the old size (or at full size) before the right one.
    function load() {
        const size = root.decodeSize
        if (size.width <= 0 || size.height <= 0)
            return
        if (current.source.toString() === root.source.toString() && current.sourceSize.width === size.width
                && current.sourceSize.height === size.height)
            return
        previous.source = ""
        if (current.status === Image.Ready) {
            previous.sourceSize = current.sourceSize
            previous.source = current.source
        }
        current.opacity = 0
        current.source = ""
        current.sourceSize = size
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
        // Hidden once the picture covers it, so its colours cannot show
        // through the picture's soft edge.
        visible: !root.loaded || current.opacity < 1
        layer.enabled: true
        // The mask's anti-aliased edge decides coverage: half coverage is the
        // edge, spread over the full soft band. With the defaults every
        // partly covered pixel counted as inside, so the corners stair-stepped.
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: mask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
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

    }

    // The pictures load here but are drawn by CoverPicture, straight from
    // their textures: inside the masked layer they were resampled twice when
    // the tile did not sit on whole pixels, which blurred them.
    Image {
        id: previous

        anchors.fill: parent
        asynchronous: true
        visible: false
    }

    Image {
        id: current

        anchors.fill: parent
        asynchronous: true
        visible: false
        opacity: 0

        onStatusChanged: {
            if (status === Image.Ready)
                root.reveal()
            else if (status === Image.Error)
                previous.source = ""
        }
    }

    CoverPicture { picture: previous }
    CoverPicture { picture: current }

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

    Component.onCompleted: root.load()
}
