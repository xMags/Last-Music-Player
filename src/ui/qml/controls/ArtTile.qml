pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Shapes
import LastMusicPlayer

// A cover: the picture at `source` with rounded corners. While the picture is
// on its way, or its owner is still loading what it belongs to (`pending`), a
// plain skeleton block holds its place; a gradient tile (chosen from
// `artworkId`) with a glyph in the middle (a music note, or a person on a
// round tile) shows only when there is no picture or it cannot load.
//
// When `source` changes to another picture, the old one stays until the new
// one has loaded and fades in over it (a song's cover giving way to the
// next). When it changes to none, as an owner clears it while it loads
// something else, the old picture goes at once: it never stands in for
// another thing.
Item {
    id: root

    property url source
    // Picks the placeholder gradient; the same id always gets the same one.
    property string artworkId
    // Large tiles (the full-size cover) draw a bigger glyph.
    property bool big: false
    // Only the gradient when there is no picture, no glyph: for tiles that
    // are a colour wash or sit inside a drawn cover.
    property bool bare: false
    property real radius: Theme.radiusMd
    property bool shadow: false
    property var shadowLayers: Theme.shadowArt
    // The owner is still loading the thing this picture belongs to, so an
    // empty `source` means "not known yet", not "none".
    property bool pending: false

    readonly property bool loaded: current.status === Image.Ready
    // No picture to wait for: none was given (and none is still to come), or
    // it could not load.
    readonly property bool pictureMissing: (root.source.toString() === "" && !root.pending)
        || current.status === Image.Error
    // The picture fully drawn over everything under it.
    readonly property bool covered: root.loaded && current.opacity >= 1
    readonly property bool round: root.radius >= Math.min(root.width, root.height) / 2 - 0.5
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
        fadeIn.stop()
        // Kept only to fade into a new picture; with none coming it goes now.
        if (current.status === Image.Ready && root.source.toString() !== "") {
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

    // The picture is on its way: one tinted block, no layer or effect, so a
    // shelf of loading covers costs next to nothing to build, with the line
    // glyph (a person on a round tile) made only while it shows. No sweep of
    // its own: bands belong to groups. Hidden once the picture covers it, so
    // it cannot show through the picture's soft edge.
    Rectangle {
        id: skeleton

        objectName: "coverSkeleton"
        anchors.fill: parent
        visible: !root.pictureMissing && !root.covered
        radius: root.radius
        color: Theme.skeleton

        // Built in the frames after the block appears, not in the turn that
        // made it: a page often starts loading many covers at once. Let go
        // when the tile goes, so a build still under way ends while the
        // tile's context is alive (see Skeleton).
        Loader {
            id: glyphLoader

            anchors.centerIn: parent
            asynchronous: true
            active: skeleton.visible && Math.min(root.width, root.height) >= 32
            sourceComponent: SkeletonGlyph {
                side: Math.min(root.width, root.height)
                person: root.round
            }
        }
    }

    // The gradient tile, for a cover with no picture: made only then, and let
    // go as soon as a picture is on its way, which frees its layers.
    Loader {
        objectName: "coverMissingTile"
        anchors.fill: parent
        active: root.pictureMissing
        sourceComponent: Item {
            Item {
                id: content

                anchors.fill: parent
                layer.enabled: true
                // The mask's anti-aliased edge decides coverage: half coverage
                // is the edge, spread over the full soft band. With the defaults
                // every partly covered pixel counted as inside, so the corners
                // stair-stepped.
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

                // The glyph: 22 px (48 px on big tiles, never over 45% of the
                // tile) in Theme.artGlyph with a soft shadow. Drawn by the
                // icon provider, so every glyph of a size and colour shares
                // one texture.
                Image {
                    id: missingGlyph

                    readonly property int glyphSize: Math.min(root.big ? 48 : 22,
                                                              Math.round(Math.min(root.width, root.height) * 0.45))
                    readonly property int pixels: Math.ceil(missingGlyph.glyphSize * Math.max(1, Screen.devicePixelRatio))
                    readonly property color tone: Theme.artGlyph

                    objectName: "coverMissingGlyph"
                    anchors.centerIn: parent
                    visible: !root.bare && missingGlyph.glyphSize > 0
                    width: missingGlyph.glyphSize
                    height: missingGlyph.glyphSize
                    opacity: missingGlyph.tone.a
                    asynchronous: true
                    sourceSize: Qt.size(missingGlyph.pixels, missingGlyph.pixels)
                    source: missingGlyph.visible
                        ? "image://icon/" + (root.round ? "person" : "music-note") + "/"
                          + Qt.rgba(missingGlyph.tone.r, missingGlyph.tone.g, missingGlyph.tone.b, 1).toString().slice(1)
                        : ""
                    layer.enabled: missingGlyph.visible
                    layer.effect: MultiEffect {
                        shadowEnabled: true
                        shadowColor: Theme.artGlyphShadow
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

            Rectangle {
                id: mask

                anchors.fill: parent
                radius: root.radius
                visible: false
                layer.enabled: true
            }
        }
    }

    // The pictures load here but are drawn by CoverPicture, straight from
    // their textures: inside the masked layer they were resampled twice when
    // the tile did not sit on whole pixels, which blurred them.
    Image {
        id: previous

        objectName: "previousPicture"
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
    Component.onDestruction: glyphLoader.active = false
}
