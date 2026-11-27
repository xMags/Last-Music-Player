import QtQuick
import LastMusicPlayer

// One group of the Settings page: its title, then its cards stacked 24px
// apart. The page lists the groups from the music source down to About.
Column {
    id: group

    property string title: ""
    default property alias cards: stack.data

    spacing: 14

    // padding-inline: 4px, as the cards' headings.
    Text {
        x: 4
        width: group.width - 2 * 4
        text: group.title
        font: Theme.settingsHeroName
        lineHeight: Theme.lineHeightSettingsHeroName
        lineHeightMode: Text.FixedHeight
        color: Theme.fg
        elide: Text.ElideRight
        Accessible.role: Accessible.Heading
        Accessible.name: group.title
    }

    Column {
        id: stack

        width: group.width
        spacing: 24
    }
}
