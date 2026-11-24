import QtQuick
import LastMusicPlayer

// Integrations (views/settings-desktop.ts renderDesktopIntegrationRows):
// Discord Rich Presence, and while it is on, what the card shows, its
// layout, what a pause does, the Cast device and a one-hour pause of
// sharing. The system media controls of the current app follow.
SettingsSection {
    id: section

    iconName: "link"
    title: qsTr("Integrations")

    readonly property bool discordOn: App.presenceAvailable && App.settings.discordPresence

    SettingsSwitchRow {
        visible: App.presenceAvailable
        title: qsTr("Discord Rich Presence")
        description: !App.settings.discordPresence ? qsTr("Show what you're listening to. Needs Discord running.")
            : App.settings.discordSnoozed ? qsTr("Sharing paused until %1").arg(App.settings.discordSnoozeEnd)
                                          : qsTr("Discord shows what you are listening to")
        checked: App.settings.discordPresence
        onChangeRequested: value => App.settings.discordPresence = value
    }

    SettingsDivider {
        visible: section.discordOn
    }

    SettingsSelectRow {
        visible: section.discordOn
        title: qsTr("Show on Discord")
        description: App.settings.discordShowSong ? qsTr("The song, artist and cover")
                                                  : qsTr("Only that you are listening, never which song")
        choices: [qsTr("Song and artist"), qsTr("Only that I'm listening")]
        currentIndex: App.settings.discordShowSong ? 0 : 1
        onActivated: index => App.settings.discordShowSong = index === 0
    }

    SettingsDivider {
        visible: section.discordOn
    }

    SettingsSelectRow {
        visible: section.discordOn
        title: qsTr("Card layout")
        description: App.settings.discordShowSong ? qsTr("How the song is laid out on your profile")
                                                  : qsTr("Not used while the song is kept private")
        selectEnabled: App.settings.discordShowSong
        choices: App.settings.discordLayoutChoices
        currentIndex: App.settings.discordLayout
        onActivated: index => App.settings.discordLayout = index
    }

    SettingsDivider {
        visible: section.discordOn
    }

    SettingsSelectRow {
        visible: section.discordOn
        title: qsTr("When paused")
        description: qsTr("What your profile shows while the music is paused")
        choices: App.settings.discordWhenPausedChoices
        currentIndex: App.settings.discordWhenPaused
        onActivated: index => App.settings.discordWhenPaused = index
    }

    SettingsDivider {
        visible: section.discordOn
    }

    SettingsSwitchRow {
        visible: section.discordOn
        title: qsTr("Show Cast device")
        description: App.settings.discordShowCastDevice ? qsTr("Adds the speaker or TV you are casting to")
                                                        : qsTr("Casting looks the same as playing here")
        checked: App.settings.discordShowCastDevice
        onChangeRequested: value => App.settings.discordShowCastDevice = value
    }

    SettingsDivider {
        visible: section.discordOn
    }

    SettingsRow {
        visible: section.discordOn
        title: qsTr("Pause sharing")
        description: App.settings.discordSnoozed ? qsTr("Nothing is shared until %1").arg(App.settings.discordSnoozeEnd)
                                                 : qsTr("Hide your listening from Discord for an hour")

        SettingsButton {
            text: App.settings.discordSnoozed ? qsTr("Resume now") : qsTr("Pause for 1 hour")
            onClicked: App.settings.discordSnoozed ? App.settings.resumeDiscordSharing()
                                                   : App.settings.pauseDiscordSharing()
        }
    }

    SettingsDivider {
        visible: App.presenceAvailable
    }

    SettingsSwitchRow {
        title: qsTr("Windows media controls")
        description: qsTr("Play and pause from the system media controls and the media keys.")
        checked: App.settings.mediaControls
        onChangeRequested: value => App.settings.mediaControls = value
    }
}
