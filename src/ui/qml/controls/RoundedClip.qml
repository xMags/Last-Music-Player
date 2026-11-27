pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// Clips its children to a rounded rectangle (CSS overflow: hidden on a box
// with border-radius). An item's own `clip` cuts to its bounding rectangle
// only, so anything filling a rounded surface shows square corners through
// it; put that content in here instead, with the surface's radius (less its
// border width when it sits inside a border). The content is drawn through a
// layer, so never put text in here: text in a layer loses its pixel-grid
// hinting and looks blurred. Clip the pictures and fills, and lay the text
// over them as a sibling.
Item {
    id: root

    property real radius: 0
    default property alias contentData: content.data

    Item {
        id: content

        anchors.fill: parent
        layer.enabled: root.radius > 0
        // The corners are worked out in the shader (a one-pixel soft edge,
        // as ArtTile's covers), so no mask layer of the same size is kept.
        layer.effect: ShaderEffect {
            readonly property size itemSize: Qt.size(width, height)
            readonly property real cornerRadius: Math.min(root.radius, width / 2, height / 2)

            fragmentShader: "qrc:/qt/qml/LastMusicPlayer/shaders/roundedclip.frag.qsb"
        }
        clip: root.radius <= 0
    }
}
