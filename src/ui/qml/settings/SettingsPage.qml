import QtQuick
import LastMusicPlayer

// Settings (views/settings.ts; views.css 1084-1171): one centred column up
// to 720px wide with the page title, then one titled card per group, 30px
// apart. Groups whose features arrive later add their own cards then.
Item {
    id: page

    implicitHeight: column.implicitHeight + 32 + 56

    Component.onCompleted: {
        App.settingsAbout.refresh()
        if (App.capabilities.chooseRegion) {
            App.regionLanguages.refresh()
        }
    }

    Connections {
        target: App

        // Signing in while Settings is open loads the account's choices.
        function onSourceChanged() {
            if (App.capabilities.chooseRegion) {
                App.regionLanguages.refresh()
            }
        }
    }

    Connections {
        target: App.source

        function onSignInRequested() {
            signIn.open()
        }
    }

    SignInDialog {
        id: signIn
    }

    Column {
        id: column

        // 32px from the panel's edges, as the other pages.
        width: Math.min(page.width - 2 * 32, 720)
        anchors.horizontalCenter: parent.horizontalCenter
        y: 32
        spacing: 30

        Text {
            // padding-inline: 4px
            x: 4
            text: qsTr("Settings")
            font: Theme.settingsHeading
            lineHeight: Theme.lineHeightSettingsHeading
            lineHeightMode: Text.FixedHeight
            color: Theme.fg
        }

        SourceModeCards {
            width: column.width
        }

        ApiKeyForm {
            width: column.width
            visible: App.source.apiKeyShown
        }

        AccountBlock {
            width: column.width
            visible: App.source.accountShown
            onSignInRequested: signIn.open()
        }

        SettingsProfileCard {
            width: column.width
            visible: App.settings.displayNameShown
        }

        SettingsLanguagesSection {
            width: column.width
            visible: App.capabilities.chooseRegion
        }

        SettingsLibraryCard {
            width: column.width
        }

        SettingsDownloadsCard {
            width: column.width
            visible: App.capabilities.downloadSongs
        }

        SettingsSection {
            width: column.width
            iconName: "play"
            title: qsTr("Playback")

            SettingsSwitchRow {
                title: qsTr("Autoplay")
                description: App.settings.autoplay
                    ? qsTr("Keep playing similar music when your queue ends")
                    : qsTr("Stop when your queue ends")
                checked: App.settings.autoplay
                onChangeRequested: value => App.settings.autoplay = value
            }

            SettingsDivider {}

            SettingsSelectRow {
                title: qsTr("Output device")
                description: qsTr("Where the sound plays.")
                choices: App.audioOutput.choices
                currentIndex: App.audioOutput.currentIndex
                onActivated: index => App.audioOutput.choose(index)
            }

            SettingsDivider {}

            SettingsSwitchRow {
                title: qsTr("Gapless playback")
                description: qsTr("Remove silence between consecutive tracks.")
                checked: App.settings.gapless
                onChangeRequested: value => App.settings.gapless = value
            }

            SettingsDivider {}

            SettingsSelectRow {
                title: qsTr("When closing window")
                description: App.settings.closeBehavior === 0
                    ? qsTr("%1 keeps playing in the notification area").arg(AppInfo.displayName)
                    : qsTr("Closing the window quits %1").arg(AppInfo.displayName)
                choices: App.settings.closeChoices
                currentIndex: App.settings.closeBehavior
                onActivated: index => App.settings.closeBehavior = index
            }
        }

        SettingsAppearanceCard {
            width: column.width
        }

        SettingsSection {
            width: column.width
            iconName: "link"
            title: qsTr("Integrations")

            SettingsSwitchRow {
                title: qsTr("Windows media controls")
                description: qsTr("Play and pause from the system media controls and the media keys.")
                checked: App.settings.mediaControls
                onChangeRequested: value => App.settings.mediaControls = value
            }
        }

        SettingsAboutCard {
            width: column.width
        }
    }
}
