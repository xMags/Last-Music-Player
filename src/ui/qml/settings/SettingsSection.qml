import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// A titled group of Settings (.settings-section, views.css 1116-1136): the
// heading (a 24px accent-tinted chip with the icon, the title, then a rule
// to the end of the line), 12px above one card that holds the rows.
Column {
    id: section

    property string iconName: ""
    property string title: ""
    default property alias rows: card.rows

    spacing: 12

    RowLayout {
        // padding-inline: 4px
        x: 4
        width: section.width - 2 * 4
        spacing: 10

        Rectangle {
            Layout.preferredWidth: 24
            Layout.preferredHeight: 24
            radius: 7
            color: Theme.accentSoft2

            Icon {
                anchors.centerIn: parent
                name: section.iconName
                size: 13
                color: Theme.accentColor
            }
        }

        Text {
            text: section.title
            font: Theme.settingsSection
            lineHeight: Theme.lineHeightSettingsSection
            lineHeightMode: Text.FixedHeight
            color: Theme.fg
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.divider
        }
    }

    SettingsCard {
        id: card

        width: section.width
    }
}
