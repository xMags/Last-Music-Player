import QtQuick
import LastMusicPlayer

// Downloads, from the current app's download settings: the folder songs are
// saved in, and the four rules (Wi-Fi only, liked songs, battery, recently
// played). Shown where songs can be downloaded.
SettingsSection {
    id: section

    iconName: "download"
    title: qsTr("Downloads")

    SettingsRow {
        title: qsTr("Download folder")
        description: App.offline.folder

        AppButton {
            text: qsTr("Change")
            onClicked: App.offline.chooseFolder()
        }
    }

    SettingsSwitchRow {
        title: qsTr("Download only on Wi-Fi")
        description: qsTr("Wait for a connection that does not count against a data plan.")
        checked: App.offline.avoidMetered
        onChangeRequested: value => App.offline.avoidMetered = value
    }

    SettingsSwitchRow {
        title: qsTr("Download liked songs")
        description: qsTr("Songs you like are saved on this PC.")
        checked: App.offline.autoLiked
        onChangeRequested: value => App.offline.autoLiked = value
    }

    SettingsSwitchRow {
        title: qsTr("Download on battery")
        description: qsTr("Keep downloading when the PC is not plugged in.")
        checked: App.offline.onBattery
        onChangeRequested: value => App.offline.onBattery = value
    }

    SettingsSwitchRow {
        title: qsTr("Keep recently played songs")
        description: qsTr("Songs you play are saved on this PC for later.")
        checked: App.offline.keepRecent
        onChangeRequested: value => App.offline.keepRecent = value
    }
}
