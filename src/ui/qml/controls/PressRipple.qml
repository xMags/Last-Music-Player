pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// The press ripple for anything pressable that is not a ButtonSurface
// (ripple.ts: nav items, rows, cards, menu items): fills its parent, starts
// from the exact press point, and only looks at the press, never takes it.
Item {
    id: root

    // The pressed item's corner radius and the colour under it.
    property real radius: 0
    property color surfaceColor: Theme.surface

    anchors.fill: parent

    // Made on the first press, in the same moment, and kept: most pressable
    // items are never pressed.
    Loader {
        id: ripple

        anchors.fill: parent
        active: false
        sourceComponent: Ripple {
            radius: root.radius
            surfaceColor: root.surfaceColor
        }
    }

    PointHandler {
        acceptedButtons: Qt.LeftButton
        enabled: root.enabled && root.visible
        onActiveChanged: {
            if (!active)
                return
            ripple.active = true
            const drop = ripple.item as Ripple
            drop.start(point.position.x, point.position.y)
        }
    }
}
