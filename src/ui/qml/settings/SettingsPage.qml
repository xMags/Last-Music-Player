import QtQuick
import LastMusicPlayer

// Settings (views/settings.ts; views.css 1084-1171): one centred column up
// to 720px wide with the page title, then one titled card per group, 30px
// apart. Groups whose features arrive later add their own cards then.
Item {
    id: page

    // padding: 8px 0 56px (.settings-page).
    implicitHeight: column.implicitHeight + 8 + 56

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

        // .settings-page inside .content: the panel's 24px gutter at the
        // sides, and only the page's own 8px above (.content has none).
        width: Math.min(page.width - 2 * 24, 720)
        anchors.horizontalCenter: parent.horizontalCenter
        y: 8
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
            id: accountCard

            width: column.width
            opacity: accountInView.inView ? 1 : 0
            visible: App.source.accountShown
            onSignInRequested: signIn.open()
        }

        SettingsPlaybackSection {
            id: playbackCard

            width: column.width
            opacity: playbackInView.inView ? 1 : 0
        }

        SettingsLanguagesSection {
            id: languagesCard

            width: column.width
            opacity: languagesInView.inView ? 1 : 0
            visible: App.capabilities.chooseRegion
        }

        SettingsAppearanceCard {
            id: appearanceCard

            width: column.width
            opacity: appearanceInView.inView ? 1 : 0
        }

        SettingsEqualizerCard {
            id: equalizerCard

            width: column.width
            opacity: equalizerInView.inView ? 1 : 0
        }

        SettingsDownloadsCard {
            id: downloadsCard

            width: column.width
            opacity: downloadsInView.inView ? 1 : 0
            visible: App.capabilities.downloadSongs
        }

        SettingsIntegrationsSection {
            id: integrationsCard

            width: column.width
            opacity: integrationsInView.inView ? 1 : 0
        }

        SettingsDataSection {
            id: dataCard

            width: column.width
            opacity: dataInView.inView ? 1 : 0
            visible: App.source.accountShown && App.source.signedIn
        }

        // The current app's own sections: where music comes from, the typed
        // name outside Account mode, the local library and About.
        SourceModeCards {
            id: sourceModeCard

            width: column.width
            opacity: sourceModeInView.inView ? 1 : 0
        }

        ApiKeyForm {
            id: apiKeyCard

            width: column.width
            opacity: apiKeyInView.inView ? 1 : 0
            visible: App.source.apiKeyShown
        }

        SettingsProfileCard {
            id: profileCard

            width: column.width
            opacity: profileInView.inView ? 1 : 0
            visible: App.settings.displayNameShown
        }

        SettingsLibraryCard {
            id: libraryCard

            width: column.width
            opacity: libraryInView.inView ? 1 : 0
        }

        SettingsAboutCard {
            id: aboutCard

            width: column.width
            opacity: aboutInView.inView ? 1 : 0
        }

        // Each card is not drawn while it lies wholly outside the visible
        // part of the page; the watches sit beside the cards, not inside
        // them, so no card's own content changes.
        InViewWatch { id: accountInView; target: accountCard }
        InViewWatch { id: playbackInView; target: playbackCard }
        InViewWatch { id: languagesInView; target: languagesCard }
        InViewWatch { id: appearanceInView; target: appearanceCard }
        InViewWatch { id: equalizerInView; target: equalizerCard }
        InViewWatch { id: downloadsInView; target: downloadsCard }
        InViewWatch { id: integrationsInView; target: integrationsCard }
        InViewWatch { id: dataInView; target: dataCard }
        InViewWatch { id: sourceModeInView; target: sourceModeCard }
        InViewWatch { id: apiKeyInView; target: apiKeyCard }
        InViewWatch { id: profileInView; target: profileCard }
        InViewWatch { id: libraryInView; target: libraryCard }
        InViewWatch { id: aboutInView; target: aboutCard }
    }
}
