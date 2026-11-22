pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import LastMusicPlayer

// A CSS box-shadow: one RectangularShadow per layer of a Theme shadow token,
// drawn in order behind the item it fills. Place it before the surface it
// belongs to so it paints underneath.
Item {
    id: root

    // A list of shadowLayer values, for example Theme.shadowCard.
    required property var layers
    property real radius: 0

    Repeater {
        model: root.layers

        RectangularShadow {
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
