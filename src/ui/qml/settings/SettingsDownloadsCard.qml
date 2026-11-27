import QtQuick
import LastMusicPlayer

// Downloads (views/settings-desktop.ts renderDesktopDownloads): what is on
// this PC with Open, the folder, then what is kept and when it downloads.
// Removing them all is in Privacy and data. Shown where songs can be
// downloaded.
SettingsSection {
    id: section

    iconName: "download"
    title: qsTr("Downloads")

    SettingsRow {
        title: qsTr("Downloaded songs")
        description: App.offline.storedSummary

        SettingsButton {
            text: qsTr("Open")
            onClicked: App.navigator.openView(Route.Offline)
        }
    }

    SettingsDivider {}

    SettingsRow {
        title: qsTr("Download folder")
        description: App.offline.folder

        SettingsButton {
            text: qsTr("Change")
            onClicked: App.offline.chooseFolder()
        }
    }

    SettingsDivider {}

    SettingsSwitchRow {
        title: qsTr("Download Liked Songs")
        description: App.offline.autoLiked ? qsTr("On · every song you like is kept on this PC")
                                           : qsTr("Off · only songs you download yourself")
        checked: App.offline.autoLiked
        onChangeRequested: value => App.offline.autoLiked = value
    }

    SettingsDivider {}

    SettingsSwitchRow {
        title: qsTr("Keep recently played songs")
        description: qsTr("Songs you play are saved on this PC for later.")
        checked: App.offline.keepRecent
        onChangeRequested: value => App.offline.keepRecent = value
    }

    SettingsDivider {}

    SettingsSwitchRow {
        title: qsTr("Download only on Wi-Fi")
        description: qsTr("Wait for a connection that does not count against a data plan.")
        checked: App.offline.avoidMetered
        onChangeRequested: value => App.offline.avoidMetered = value
    }

    SettingsDivider {}

    SettingsSwitchRow {
        title: qsTr("Download on battery")
        description: qsTr("Keep downloading when the PC is not plugged in.")
        checked: App.offline.onBattery
        onChangeRequested: value => App.offline.onBattery = value
    }
}
