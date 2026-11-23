import QtQuick
import LastMusicPlayer

// A titled group of Settings (.settings-section, views.css 1116-1136): the
// heading 12px above one card that holds the rows.
Column {
    id: section

    property string iconName: ""
    property string title: ""
    property string note: ""
    default property alias rows: card.rows

    spacing: 12

    SettingsSectionHead {
        width: section.width
        iconName: section.iconName
        title: section.title
        note: section.note
    }

    SettingsCard {
        id: card

        width: section.width
    }
}
