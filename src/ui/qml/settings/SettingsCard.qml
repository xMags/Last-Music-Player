pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// One card of Settings: a 12px-rounded surface, its parts stacked inside,
// and a 1px line above every part but the first (and but a SettingsBlock
// `joined` to the row above), across the whole card. A
// danger card has a faint red wash and border. In the light theme, where
// the card and the page are both near white, a card also has a 1px border
// and the soft card shadow. `hasRows` says whether any part other than the
// label shows (a search can hide them all).
Item {
    id: card

    default property alias rows: column.data
    // The card of what cannot be undone.
    property bool danger: false

    readonly property var shownParts: column.visibleChildren.filter(item => item.height > 0)
    readonly property bool hasRows: shownParts.some(item => item.objectName !== "settingsCardLabel")

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

    Repeater {
        model: card.shownParts

        Rectangle {
            required property Item modelData
            required property int index

            visible: index > 0 && !(modelData instanceof SettingsBlock && (modelData as SettingsBlock).joined)
            y: modelData.y
            width: card.width
            height: 1
            color: card.danger ? SettingsStyle.dangerLine : SettingsStyle.line
        }
    }
}
