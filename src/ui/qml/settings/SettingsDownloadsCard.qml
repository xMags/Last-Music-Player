import QtQuick
import LastMusicPlayer

// Downloads (views/settings-desktop.ts renderDesktopDownloads): what is on
// this PC with Open, keeping Liked Songs downloaded, and, while anything is
// on the list, Remove all downloads, which asks once on the button itself.
// The current app's folder and rules follow. Shown where songs can be
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

    SettingsSwitchRow {
        title: qsTr("Download Liked Songs")
        description: App.offline.autoLiked ? qsTr("On · every song you like is kept on this PC")
                                           : qsTr("Off · only songs you download yourself")
        checked: App.offline.autoLiked
        onChangeRequested: value => App.offline.autoLiked = value
    }

    SettingsDivider {
        visible: App.offline.hasAnyDownloads
    }

    SettingsRow {
        visible: App.offline.hasAnyDownloads
        title: qsTr("Remove all downloads")
        description: qsTr("Frees the space they use. Songs stay in your library.")

        // Asked in a dialog, as the other rows that erase things are: a
        // two-press button would be disarmed by every download tick, and
        // left armed forever otherwise.
        SettingsButton {
            danger: true
            text: qsTr("Remove all")
            onClicked: removeDialog.open()
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

    SettingsDivider {}

    SettingsSwitchRow {
        title: qsTr("Keep recently played songs")
        description: qsTr("Songs you play are saved on this PC for later.")
        checked: App.offline.keepRecent
        onChangeRequested: value => App.offline.keepRecent = value
    }

    SettingsConfirmDialog {
        id: removeDialog

        title: qsTr("Remove all downloads?")
        description: qsTr("This deletes every downloaded song from this PC. Songs stay in your library and can be downloaded again.")
        confirmLabel: qsTr("Remove all")
        onConfirmed: App.offline.removeAll()
    }
}
