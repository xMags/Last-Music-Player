pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// One card of Settings (.settings-card, views.css 1138-1147): a rounded
// surface with a thin border and a soft shadow, its rows stacked inside.
// Children go into the column; put a SettingsDivider between two rows.
Item {
    id: card

    default property alias rows: column.data
    // The account hero (.settings-hero, views.css 1252-1259): the surface
    // fades down to the alternate surface, under a 1px accent highlight
    // along the top that fades out to both sides.
    property bool hero: false
    // The card of what cannot be undone: a faint red wash and border.
    property bool danger: false

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
        color: card.danger ? Theme.mixSrgb(Theme.danger, Theme.surface, 0.05) : Theme.surface
        gradient: card.hero ? heroFill : null
        border.width: 1
        border.color: card.danger ? Theme.mixSrgb(Theme.danger, "transparent", 0.35) : Theme.stroke
    }

    Gradient {
        id: heroFill

        GradientStop { position: 0; color: Theme.surface }
        GradientStop { position: 1; color: Theme.surfaceAlt }
    }

    // ::before: inset 0 0 auto inside the border, 1px high.
    Rectangle {
        id: heroGlow

        // CSS fades to transparent in premultiplied colour; the same hue at
        // zero alpha keeps Qt's gradient from greying the ends.
        readonly property color glow: Theme.mixSrgb(Theme.accentFill, "transparent", 0.6)

        visible: card.hero
        x: 1
        y: 1
        width: parent.width - 2
        height: 1
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: Qt.alpha(heroGlow.glow, 0) }
            GradientStop { position: 0.5; color: heroGlow.glow }
            GradientStop { position: 1; color: Qt.alpha(heroGlow.glow, 0) }
        }
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
