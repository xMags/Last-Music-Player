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

        // The reference's sections, in its order.
        AccountBlock {
            width: column.width
            visible: App.source.accountShown
            onSignInRequested: signIn.open()
        }

        SettingsPlaybackSection {
            width: column.width
        }

        SettingsLanguagesSection {
            width: column.width
            visible: App.capabilities.chooseRegion
        }

        SettingsAppearanceCard {
            width: column.width
        }

        SettingsEqualizerCard {
            width: column.width
        }

        SettingsDownloadsCard {
            width: column.width
            visible: App.capabilities.downloadSongs
        }

        SettingsIntegrationsSection {
            width: column.width
        }

        SettingsDataSection {
            width: column.width
            visible: App.source.accountShown && App.source.signedIn
        }

        // The current app's own sections: where music comes from, the typed
        // name outside Account mode, the local library and About.
        SourceModeCards {
            width: column.width
        }

        ApiKeyForm {
            width: column.width
            visible: App.source.apiKeyShown
        }

        SettingsProfileCard {
            width: column.width
            visible: App.settings.displayNameShown
        }

        SettingsLibraryCard {
            width: column.width
        }

        SettingsAboutCard {
            width: column.width
        }
    }
}
