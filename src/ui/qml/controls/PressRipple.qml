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

    Ripple {
        id: ripple

        radius: root.radius
        surfaceColor: root.surfaceColor
    }

    PointHandler {
        acceptedButtons: Qt.LeftButton
        enabled: root.enabled && root.visible
        onActiveChanged: {
            if (active)
                ripple.start(point.position.x, point.position.y)
        }
    }
}
