import QtQuick
import LastMusicPlayer

// Playback: Autoplay, gapless playback, Data saver where streams have a
// lower quality, the output device and what closing the window does.
SettingsSection {
    title: qsTr("Playback")

    SettingsSwitchRow {
        title: qsTr("Autoplay")
        description: App.settings.autoplay ? qsTr("Keep playing similar music when your queue ends")
                                           : qsTr("Stop when your queue ends")
        checked: App.settings.autoplay
        onChangeRequested: value => App.settings.autoplay = value
    }

    SettingsSwitchRow {
        title: qsTr("Gapless playback")
        description: qsTr("Remove silence between consecutive tracks.")
        checked: App.settings.gapless
        onChangeRequested: value => App.settings.gapless = value
    }

    SettingsSwitchRow {
        shown: App.capabilities.chooseLowQualityStreams
        title: qsTr("Data saver")
        description: App.settings.dataSaver ? qsTr("Active · lower-quality audio for songs that are not cached")
                                            : qsTr("Use less data with lower-quality audio")
        checked: App.settings.dataSaver
        onChangeRequested: value => App.settings.dataSaver = value
    }

    SettingsSelectRow {
        title: qsTr("Output device")
        description: qsTr("Where the sound plays.")
        choices: App.audioOutput.choices
        currentIndex: App.audioOutput.currentIndex
        onActivated: index => App.audioOutput.choose(index)
    }

    SettingsSelectRow {
        title: qsTr("When closing the window")
        description: App.settings.closeBehavior === 0
            ? qsTr("%1 keeps playing in the notification area").arg(AppInfo.displayName)
            : qsTr("Closing the window quits %1").arg(AppInfo.displayName)
        choices: App.settings.closeChoices
        currentIndex: App.settings.closeBehavior
        onActivated: index => App.settings.closeBehavior = index
    }
}
