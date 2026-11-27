import QtQuick
import LastMusicPlayer

// Settings (views/settings.ts; views.css 1084-1171): one continuous, centred
// column up to 720px wide with the page title, then the groups 36px apart:
// source and account, playback and sound, library and downloads, look and
// language, connections, privacy and data, and About.
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
        spacing: 36

        Text {
            // padding-inline: 4px
            x: 4
            text: qsTr("Settings")
            font: Theme.settingsHeading
            lineHeight: Theme.lineHeightSettingsHeading
            lineHeightMode: Text.FixedHeight
            color: Theme.fg
        }

        // The groups, from where music comes from down to About; each card
        // keeps its own heading unless the group's title already names it.
        SettingsGroup {
            id: sourceGroup

            width: column.width
            opacity: sourceInView.inView ? 1 : 0
            title: qsTr("Music source")
            note: qsTr("One source at a time. Songs on this PC and downloads play in all three.")

            SourceModeCards {
                width: parent.width
            }

            AccountBlock {
                width: parent.width
                visible: App.source.accountShown
                onSignInRequested: signIn.open()
            }

            ApiKeyForm {
                width: parent.width
                visible: App.source.apiKeyShown
            }

            SettingsProfileCard {
                width: parent.width
                visible: App.settings.displayNameShown
            }
        }

        SettingsGroup {
            id: playbackGroup

            width: column.width
            opacity: playbackInView.inView ? 1 : 0
            title: qsTr("Playback and sound")

            SettingsPlaybackSection {
                width: parent.width
            }

            SettingsEqualizerCard {
                width: parent.width
            }
        }

        SettingsGroup {
            id: libraryGroup

            width: column.width
            opacity: libraryInView.inView ? 1 : 0
            title: qsTr("Library and downloads")

            SettingsLibraryCard {
                width: parent.width
            }

            SettingsDownloadsCard {
                width: parent.width
                visible: App.capabilities.downloadSongs
            }
        }

        SettingsGroup {
            id: lookGroup

            width: column.width
            opacity: lookInView.inView ? 1 : 0
            title: qsTr("Look and language")

            SettingsAppearanceCard {
                width: parent.width
            }

            SettingsLanguagesSection {
                width: parent.width
                visible: App.capabilities.chooseRegion
            }
        }

        SettingsGroup {
            id: connectionsGroup

            width: column.width
            opacity: connectionsInView.inView ? 1 : 0
            title: qsTr("Connections")

            SettingsIntegrationsSection {
                width: parent.width
            }
        }

        SettingsGroup {
            id: dataGroup

            width: column.width
            opacity: dataInView.inView ? 1 : 0
            title: qsTr("Privacy and data")

            SettingsDataSection {
                width: parent.width
            }
        }

        SettingsGroup {
            id: aboutGroup

            width: column.width
            opacity: aboutInView.inView ? 1 : 0
            title: qsTr("About")

            SettingsAboutCard {
                width: parent.width
            }
        }

        // Each group is not drawn while it lies wholly outside the visible
        // part of the page; the watches sit beside the groups, not inside
        // them, so no card's own content changes.
        InViewWatch { id: sourceInView; target: sourceGroup }
        InViewWatch { id: playbackInView; target: playbackGroup }
        InViewWatch { id: libraryInView; target: libraryGroup }
        InViewWatch { id: lookInView; target: lookGroup }
        InViewWatch { id: connectionsInView; target: connectionsGroup }
        InViewWatch { id: dataInView; target: dataGroup }
        InViewWatch { id: aboutInView; target: aboutGroup }
    }
}
