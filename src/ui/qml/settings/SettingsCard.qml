pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// One card of Settings: a 12px-rounded surface, its parts stacked inside;
// each row draws the 1px line above itself (SettingsRowLine). A
// danger card has a faint red wash and border. In the light theme, where
// the card and the page are both near white, a card also has a 1px border
// and the soft card shadow. `hasRows` says whether any part other than the
// label shows (a search can hide them all).
Item {
    id: card

    default property alias rows: column.data
    // The card of what cannot be undone.
    property bool danger: false

    // Read only while the page is searched (SettingsSection.visible).
    readonly property bool hasRows: column.visibleChildren.some(item => item.height > 0 && item.objectName !== "settingsCardLabel")

    implicitHeight: column.implicitHeight

    BoxShadow {
        anchors.fill: parent
        visible: !Theme.dark && !card.danger
        radius: 12
        layers: Theme.shadowCard
    }

    Rectangle {
        anchors.fill: parent
        radius: 12
        color: card.danger ? SettingsStyle.dangerFill : SettingsStyle.card
        border.width: card.danger || !Theme.dark ? 1 : 0
        border.color: card.danger ? SettingsStyle.dangerBorder : Theme.stroke
    }

    // The rows draw straight into the window, never through a layer: text
    // in a layer loses its pixel-grid hinting and looks blurred.
    Column {
        id: column

        objectName: "settingsCardRows"
        width: parent.width
    }

}
