pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// Appearance: the theme (its words, then the three tiles across the card,
// 12px apart), the window background, the accent swatches and the cover on
// the player bar.
SettingsSection {
    title: qsTr("Appearance")

    SettingsBlock {
        keywords: qsTr("Theme") + "\n" + qsTr("Light, dark, or follow Windows")

        ColumnLayout {
            width: parent.width
            spacing: 12

            SettingsRowText {
                Layout.fillWidth: true
                title: qsTr("Theme")
                description: qsTr("Light, dark, or follow Windows")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Repeater {
                    model: [
                        { mode: "light", stored: "Light", label: qsTr("Light") },
                        { mode: "dark", stored: "Dark", label: qsTr("Dark") },
                        { mode: "system", stored: "System", label: qsTr("System") }
                    ]

                    ThemeTile {
                        required property var modelData

                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        mode: modelData.mode
                        text: modelData.label
                        selected: App.settings.theme === modelData.stored
                        onClicked: App.settings.theme = modelData.stored
                    }
                }
            }
        }
    }

    SettingsSelectRow {
        // The stored strings, in the order of the choices.
        readonly property var stored: ["Solid", "Mica", "Acrylic"]

        title: qsTr("Window background")
        segmented: true
        description: App.windowChrome.backdropNeedsRestart ? qsTr("Restart Last Music Player to see it")
            : App.settings.windowBackdrop === "Solid" ? qsTr("A plain sidebar and title bar")
            : App.settings.windowBackdrop === "Mica" ? qsTr("The sidebar takes a soft tint of your wallpaper")
            : qsTr("The sidebar shows a blur of what is behind the window")
        choices: [qsTr("Solid"), qsTr("Mica"), qsTr("Acrylic")]
        currentIndex: Math.max(0, stored.indexOf(App.settings.windowBackdrop))
        onActivated: index => App.settings.windowBackdrop = stored[index]
    }

    SettingsRow {
        title: qsTr("Accent color")
        description: qsTr("Highlights, sliders and the progress bar")

        Row {
            spacing: 10

            Repeater {
                model: App.settings.accentSeeds

                SettingsSwatch {
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

    SettingsSwitchRow {
        title: qsTr("Album art on player bar")
        description: qsTr("Display artwork next to the now-playing track.")
        checked: App.settings.showAlbumArt
        onChangeRequested: value => App.settings.showAlbumArt = value
    }
}
