import QtQuick
import QtQuick.Effects
import LastMusicPlayer

// Clips its children to a rounded rectangle (CSS overflow: hidden on a box
// with border-radius). An item's own `clip` cuts to its bounding rectangle
// only, so anything filling a rounded surface shows square corners through
// it; put that content in here instead, with the surface's radius (less its
// border width when it sits inside a border).
Item {
    id: root

    property real radius: 0
    default property alias contentData: content.data

    Item {
        id: content

        anchors.fill: parent
        layer.enabled: root.radius > 0
        // The mask's anti-aliased edge decides coverage, as in ArtTile: half
        // coverage is the edge, spread over the full soft band.
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: mask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }
        clip: root.radius <= 0
    }

    Rectangle {
        id: mask

        anchors.fill: parent
        radius: root.radius
        visible: false
        layer.enabled: true
    }
}
