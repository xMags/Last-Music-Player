import QtQuick

// A cover's colour wash stretched over the item (cover-wash.ts): the
// picture drawn tiny, softened and more saturated by the cover providers'
// "-wash" variants (ui/images/CoverWash.h), so nothing is blurred on screen.
// A source those providers do not serve shows as the picture drawn tiny.
Item {
    id: wash

    property url source
    // The wash's size in pixels: its blur is 1.2 of them, so a smaller wash
    // stretched over the same item is softer.
    property size washSize: Qt.size(16, 16)

    readonly property url washSource: {
        const text = wash.source.toString()
        for (const provider of ["artwork", "embedded-cover"]) {
            const prefix = "image://" + provider + "/"
            if (text.startsWith(prefix))
                return "image://" + provider + "-wash/" + text.slice(prefix.length)
        }
        return wash.source
    }

    Image {
        anchors.fill: parent
        source: wash.washSource
        sourceSize: wash.washSize
        fillMode: Image.Stretch
        smooth: true
        asynchronous: true
        visible: status === Image.Ready
    }
}
