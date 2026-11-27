import QtQuick
import LastMusicPlayer

// One group of the Settings page: its title (and a line under it), then its
// cards stacked 16px apart. The page lists the groups from the music source
// down to About.
Column {
    id: group

    property string title: ""
    property string note: ""
    default property alias cards: stack.data

    spacing: 16

    // padding-inline: 4px, as the cards' rows.
    Column {
        x: 4
        width: group.width - 2 * 4
        spacing: 4

        Text {
            width: parent.width
            text: group.title
            font.family: Theme.settingsSection.family
            font.pixelSize: 20
            font.weight: Font.DemiBold
            color: Theme.fg
            elide: Text.ElideRight
            Accessible.role: Accessible.Heading
            Accessible.name: group.title
        }

        Text {
            width: parent.width
            visible: group.note !== ""
            text: group.note
            font: Theme.settingsRowDescription
            lineHeight: Theme.lineHeightSettingsRowDescription
            lineHeightMode: Text.FixedHeight
            color: Theme.fgTertiary
            wrapMode: Text.Wrap
        }
    }

    Column {
        id: stack

        width: group.width
        spacing: 16
    }
}
