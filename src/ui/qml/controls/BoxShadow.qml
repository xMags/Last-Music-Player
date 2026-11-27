pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// A CSS box-shadow: one RectangularShadow per layer of a Theme shadow token,
// drawn in order behind the item it fills. Place it before the surface it
// belongs to so it paints underneath. The layers exist only while it shows.
Item {
    id: root

    // A list of shadowLayer values, for example Theme.shadowCard.
    required property var layers
    property real radius: 0

    Repeater {
        // None while hidden: most shadows are (a tile without one, a row
        // that is not lifted), and each layer is a shader effect.
        model: root.visible ? root.layers : []

        AppRectangularShadow {
            required property shadowLayer modelData

            anchors.fill: parent
            offset.x: modelData.offsetX
            offset.y: modelData.offsetY
            blur: modelData.blur
            spread: modelData.spread
            color: modelData.color
            radius: root.radius
        }
    }
}
