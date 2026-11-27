import QtQuick
import LastMusicPlayer

// What a row of a Settings card shares: its own condition (`shown`), and
// whether a search hides it. While the page is searched a row shows when
// its card or group matches (the card's `searchForced`), or when its own
// `searchText` does. The card is found once the row is placed.
QtObject {
    id: searchable

    required property Item row
    property string searchText: ""

    property SettingsSection hostSection: null
    readonly property bool matches: SettingsStyle.needle === ""
        || (searchable.hostSection !== null && searchable.hostSection.searchForced)
        || SettingsStyle.matches(searchable.searchText)

    Component.onCompleted: {
        for (let up = searchable.row.parent; up; up = up.parent) {
            if (up instanceof SettingsSection) {
                searchable.hostSection = up as SettingsSection
                return
            }
        }
    }
}
