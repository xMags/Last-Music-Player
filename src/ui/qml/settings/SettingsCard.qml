pragma ComponentBehavior: Bound

import QtQuick
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

    // The rows draw straight into the window, never through a layer: text
    // in a layer loses its pixel-grid hinting and looks blurred. A row that
    // fills its background (SettingsSwitchRow) rounds its own corners when it
    // is the first or last one here, which is what overflow: hidden gave.
    Column {
        id: column

        objectName: "settingsCardRows"
        // Inside the 1px border, as a box's content is.
        x: 1
        y: 1
        width: parent.width - 2
    }
}
