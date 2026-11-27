import QtQuick
import LastMusicPlayer

// Privacy and data: everything that removes something, mildest first, in
// four cards. Your data: the data folder. Clear: the synced listening
// history (signed in) and every download (while there are any). Reset: the
// settings' defaults, and the app's data on this PC (signed in). Delete
// everything, in red: all data, which asks for CLEAN UP to be typed. Each asks first; while an account step runs both of those wait and
// the running one says so; how it went shows under the card. Music files on
// disk are never deleted.
Column {
    id: section

    readonly property bool idle: App.dataPrivacy.working === ""
    readonly property bool accountData: App.source.accountShown && App.source.signedIn

    spacing: 20

    SettingsSection {
        width: section.width
        title: qsTr("Your data")

        SettingsRow {
            title: qsTr("Data folder")
            description: qsTr("Where the library, settings and caches are kept on this PC")

            SettingsButton {
                text: qsTr("Open data folder")
                onClicked: App.settingsAbout.openDataFolder()
            }
        }
    }

    SettingsSection {
        width: section.width
        visible: section.accountData || App.offline.hasAnyDownloads
        title: qsTr("Clear")

        SettingsRow {
            visible: section.accountData
            title: qsTr("Clear listening history")
            description: App.dataPrivacy.historySubtitle

            SettingsButton {
                enabled: App.dataPrivacy.hasHistory && section.idle
                working: App.dataPrivacy.working === "history"
                text: working ? qsTr("Working…") : qsTr("Clear history")
                onClicked: historyDialog.open()
            }
        }

        SettingsDivider {
            visible: section.accountData && App.offline.hasAnyDownloads
        }

        SettingsRow {
            visible: App.offline.hasAnyDownloads
            title: qsTr("Remove all downloads")
            description: qsTr("Frees the space they use. Songs stay in your library.")

            // Asked in a dialog: a two-press button would be disarmed by
            // every download tick, and left armed forever otherwise.
            SettingsButton {
                text: qsTr("Remove all")
                onClicked: removeDownloadsDialog.open()
            }
        }
    }

    SettingsSection {
        width: section.width
        title: qsTr("Reset")
        // How clearing the history or resetting the app went.
        feedback: App.dataPrivacy.message
        feedbackError: App.dataPrivacy.messageError

        SettingsRow {
            title: qsTr("Reset settings")
            description: qsTr("Restore all preferences to their defaults. Your music and data are not affected.")

            SettingsButton {
                text: qsTr("Reset settings")
                onClicked: resetSettingsDialog.open()
            }
        }

        SettingsDivider {
            visible: section.accountData
        }

        SettingsRow {
            visible: section.accountData
            title: qsTr("Reset app data")
            description: qsTr("Sign out and erase settings, queue, artwork and audio caches on this computer. Your synced likes, playlists and history stay online.")

            SettingsButton {
                enabled: section.idle
                working: App.dataPrivacy.working === "reset"
                text: working ? qsTr("Working…") : qsTr("Reset app data")
                onClicked: resetDialog.open()
            }
        }
    }

    SettingsSection {
        width: section.width
        danger: true
        title: qsTr("Delete everything")

        SettingsRow {
            title: qsTr("Delete all data")
            description: App.dataSettings.status !== "" ? App.dataSettings.status
                : qsTr("Clears the library, history, likes, playlists, queues, settings, sign-in and caches. Music files on disk are not deleted.")

            SettingsButton {
                danger: true
                enabled: !App.dataSettings.busy
                working: App.dataSettings.busy
                text: working ? qsTr("Cleaning up…") : qsTr("Delete all data")
                onClicked: {
                    cleanupDialog.text = ""
                    cleanupDialog.errorText = ""
                    cleanupDialog.open()
                }
            }
        }
    }

    SettingsConfirmDialog {
        id: historyDialog

        title: qsTr("Clear listening history?")
        description: qsTr("This permanently removes your synced play history and resets Listen Again and Most Played. Liked songs and playlists stay intact.")
        confirmLabel: qsTr("Clear history")
        onConfirmed: App.dataPrivacy.clearHistory()
    }

    SettingsConfirmDialog {
        id: removeDownloadsDialog

        title: qsTr("Remove all downloads?")
        description: qsTr("This deletes every downloaded song from this PC. Songs stay in your library and can be downloaded again.")
        confirmLabel: qsTr("Remove all")
        onConfirmed: App.offline.removeAll()
    }

    SettingsConfirmDialog {
        id: resetSettingsDialog

        title: qsTr("Reset settings?")
        description: qsTr("Restore all preferences to their defaults? Your music library and data are not affected.")
        confirmLabel: qsTr("Reset")
        onConfirmed: App.settings.resetDefaults()
    }

    SettingsConfirmDialog {
        id: resetDialog

        title: qsTr("Reset %1?").arg(AppInfo.displayName)
        description: qsTr("This signs you out and erases %1 settings, queue, and cached audio on this device. Your synced likes, playlists, and history stay online.").arg(AppInfo.displayName)
        confirmLabel: qsTr("Reset app")
        onConfirmed: App.dataPrivacy.resetAppData()
    }

    TextPromptDialog {
        id: cleanupDialog

        title: qsTr("Delete all data?")
        description: qsTr("This clears the local library database, playback history, liked tracks, playlists, queues, settings, provider config and app caches. Music files on disk are not deleted.")
        fieldLabel: qsTr("Type CLEAN UP to confirm")
        placeholderText: "CLEAN UP"
        submitLabel: qsTr("Delete all data")
        onSubmitted: value => {
            if (App.dataSettings.deleteAll(value))
                cleanupDialog.finish()
            else
                cleanupDialog.errorText = qsTr("Type CLEAN UP in capitals to confirm.")
        }
    }
}
