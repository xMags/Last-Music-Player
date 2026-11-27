import QtQuick
import LastMusicPlayer

// One group of the Settings page: its 20px title (and a line under it),
// then its cards, 20px apart. While the page is searched the group shows
// all its cards when its title matches, else only the cards that match,
// and hides when none does.
Column {
    id: group

    property string title: ""
    property string note: ""
    property bool shown: true
    default property alias cards: stack.data

    // Read by the cards inside (SettingsSection.hostGroup).
    readonly property bool groupSearchForced: SettingsStyle.needle === "" || SettingsStyle.matches(group.title)

    visible: shown && (SettingsStyle.needle === "" || stack.visibleChildren.some(item => item.height > 0))
    spacing: 20

    Column {
        width: group.width
        spacing: 6

        AppText {
            width: parent.width
            text: group.title
            font.family: SettingsStyle.family
            font.pixelSize: 20
            font.weight: Font.DemiBold
            color: SettingsStyle.title
            elide: Text.ElideRight
            Accessible.role: Accessible.Heading
            Accessible.name: group.title
        }

        AppText {
            width: parent.width
            visible: group.note !== ""
            text: group.note
            font.family: SettingsStyle.family
            font.pixelSize: 13
            color: SettingsStyle.description
            wrapMode: Text.Wrap
        }
    }

    Column {
        id: stack

        width: group.width
        spacing: 20
    }
}
