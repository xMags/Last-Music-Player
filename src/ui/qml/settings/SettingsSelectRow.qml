import QtQuick
import LastMusicPlayer

// A row whose control is a dropdown, or with `segmented` a row of choices.
// `activated` reports the chosen index; the owner writes it and binds
// `currentIndex` back.
SettingsRow {
    id: row

    property var choices: []
    property int currentIndex: 0
    property bool segmented: false
    // The dropdown alone can be turned off.
    property bool selectEnabled: true

    signal activated(int index)

    // A Row sizes to the shown control only: the hidden one must not widen
    // the row's control area and squeeze out its words.
    Row {
        SettingsSelect {
            visible: !row.segmented
            enabled: row.selectEnabled
            model: row.choices
            currentIndex: row.currentIndex
            Accessible.name: row.title
            onActivated: index => row.activated(index)
        }

        SettingsSegmented {
            visible: row.segmented
            enabled: row.selectEnabled
            choices: row.choices
            currentIndex: row.currentIndex
            Accessible.name: row.title
            onActivated: index => row.activated(index)
        }
    }
}
