pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import LastMusicPlayer

// One card of Settings (.settings-card, views.css 1138-1147): a rounded
// surface with a thin border and a soft shadow, its rows stacked inside.
// Children go into the column; put a SettingsDivider between two rows.
Item {
    id: card

    default property alias rows: column.data

    implicitHeight: column.implicitHeight + 2

    BoxShadow {
        anchors.fill: surface
        layers: Theme.shadowCard
        radius: surface.radius
    }

    Rectangle {
        id: surface

        anchors.fill: parent
        radius: Theme.radiusLg
        color: Theme.surface
        border.width: 1
        border.color: Theme.stroke
    }

    // The rows are clipped to the card's corners (overflow: hidden), so a
    // hovered first or last row keeps the rounded edge. Half coverage of the
    // mask's soft edge is the edge (see ArtTile).
    Column {
        id: column

        // Inside the 1px border, as a box's content is.
        x: 1
        y: 1
        width: parent.width - 2
        layer.enabled: true
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: mask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }
    }

    Rectangle {
        id: mask

        width: column.width
        height: column.height
        radius: surface.radius - 1
        visible: false
        layer.enabled: true
    }
}
