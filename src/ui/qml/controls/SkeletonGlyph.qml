import QtQuick
import LastMusicPlayer

// The line glyph in the middle of a loading cover: a music note, or a person
// on a round (artist) tile, in Theme.skeletonGlyph. About 27% of the cover's
// side and never under 18 px; none on covers under 32 px. Drawn from the
// icon provider, so every glyph of a size shares one texture. The parent
// places it (a Loader centred on the cover).
Image {
    id: glyph

    // The side of the cover it sits in.
    required property real side
    property bool person: false

    readonly property int glyphSize: Math.max(18, Math.round(glyph.side * 0.27))
    readonly property int pixels: Math.ceil(glyph.glyphSize * Math.max(1, Screen.devicePixelRatio))

    width: glyph.glyphSize
    height: glyph.glyphSize
    visible: glyph.side >= 32
    asynchronous: true
    sourceSize: Qt.size(glyph.pixels, glyph.pixels)
    source: glyph.visible ? "image://icon/" + (glyph.person ? "person" : "music-note") + "/"
                            + Theme.skeletonGlyph.toString().slice(1) + "/outline" : ""
}
