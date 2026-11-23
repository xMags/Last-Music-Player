pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// Appearance (views/settings.ts 172-191): the theme tiles, the accent
// swatches and, from the current app, the cover on the player bar.
SettingsSection {
    iconName: "sparkle"
    title: qsTr("Appearance")

    SettingsRow {
        title: qsTr("Theme")
        description: qsTr("Light, dark, or follow Windows")

        Row {
            spacing: 8

            Repeater {
                model: [
                    { mode: "light", stored: "Light", label: qsTr("Light") },
                    { mode: "dark", stored: "Dark", label: qsTr("Dark") },
                    { mode: "system", stored: "System", label: qsTr("System") }
                ]

                ThemeTile {
                    required property var modelData

                    mode: modelData.mode
                    text: modelData.label
                    selected: App.settings.theme === modelData.stored
                    onClicked: App.settings.theme = modelData.stored
                }
            }
        }
    }

    SettingsDivider {}

    SettingsRow {
        title: qsTr("Accent color")
        description: qsTr("Highlights, sliders and the progress bar")

        Row {
            spacing: 4

            Repeater {
                model: App.settings.accentSeeds

                AccentSwatch {
                    required property int index
                    required property color modelData

                    seed: modelData
                    selected: App.settings.accentIndex === index
                    Accessible.name: App.settings.accentNames[index]
                    onClicked: App.settings.accentIndex = index
                }
            }
        }
    }

    SettingsDivider {}

    SettingsSwitchRow {
        title: qsTr("Album art on player bar")
        description: qsTr("Display artwork next to the now-playing track.")
        checked: App.settings.showAlbumArt
        onChangeRequested: value => App.settings.showAlbumArt = value
    }
}
