import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// Settings: one continuous, centred column up to 820px wide, 32px from the
// top and 40px from the sides. The 30px title with the search box across
// from it, then the groups 48px apart: music source (the three sources and
// the card of the one in use), playback and sound, library and downloads,
// look and language, connections, privacy and data, and About. Typing in
// the search box keeps only the rows, cards and groups that match.
Item {
    id: page

    implicitHeight: column.implicitHeight + 32 + 48

    Component.onCompleted: {
        App.settingsAbout.refresh()
        if (App.capabilities.chooseRegion) {
            App.regionLanguages.refresh()
        }
    }

    // A search is for this visit to the page.
    Component.onDestruction: SettingsStyle.query = ""

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

    // Brings a group to the top of the page's scroll view, 24px under it.
    function scrollTo(target: Item) {
        for (let up = page.parent; up; up = up.parent) {
            if (up instanceof Flickable) {
                const view = up as Flickable
                const y = target.mapToItem(view.contentItem, 0, 0).y - 24
                view.contentY = Math.max(0, Math.min(y, view.contentHeight - view.height))
                return
            }
        }
    }

    Column {
        id: column

        width: Math.min(page.width - 2 * 40, 820)
        anchors.horizontalCenter: parent.horizontalCenter
        y: 32
        spacing: 28

        RowLayout {
            width: column.width
            spacing: 16

            Text {
                Layout.fillWidth: true
                text: qsTr("Settings")
                font.family: SettingsStyle.family
                font.pixelSize: 30
                font.weight: Font.Bold
                color: SettingsStyle.title
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
            }

            // The search box: 40px high and round, up to 340px wide.
            Rectangle {
                Layout.preferredWidth: Math.min(340, column.width / 2)
                Layout.preferredHeight: 40
                radius: 20
                color: SettingsStyle.card
                border.width: 1
                border.color: searchInput.activeFocus ? Theme.accentColor : SettingsStyle.quiet

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 10

                    Icon {
                        name: "browse"
                        size: 16
                        color: SettingsStyle.description
                    }

                    TextInput {
                        id: searchInput

                        Layout.fillWidth: true
                        font.family: SettingsStyle.family
                        font.pixelSize: 14
                        color: SettingsStyle.title
                        selectionColor: Theme.accentSoft
                        selectedTextColor: SettingsStyle.title
                        selectByMouse: true
                        clip: true
                        Accessible.role: Accessible.EditableText
                        Accessible.name: qsTr("Search settings")
                        onTextChanged: SettingsStyle.query = text
                        Keys.onEscapePressed: event => {
                            if (searchInput.text === "") {
                                event.accepted = false
                                return
                            }
                            searchInput.text = ""
                        }

                        Text {
                            anchors.fill: parent
                            visible: searchInput.text === ""
                            text: qsTr("Search settings")
                            font: searchInput.font
                            color: SettingsStyle.description
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }

                    IconButton {
                        visible: searchInput.text !== ""
                        iconName: "close"
                        shape: IconButton.Square
                        Accessible.name: qsTr("Clear search")
                        onClicked: searchInput.text = ""
                    }
                }

                HoverHandler {
                    cursorShape: Qt.IBeamCursor
                }

                TapHandler {
                    onTapped: searchInput.forceActiveFocus()
                }
            }
        }

        Column {
            id: groups

            width: column.width
            spacing: 48

            SettingsGroup {
                id: sourceGroup

                width: groups.width
                opacity: sourceInView.inView ? 1 : 0
                title: qsTr("Music source")
                note: qsTr("One source at a time. Songs on this PC and downloads play in all three.")

                SourceModeCards {
                    width: parent.width
                }

                AccountBlock {
                    width: parent.width
                    shown: App.source.accountShown
                    onSignInRequested: signIn.open()
                }

                ApiKeyForm {
                    width: parent.width
                    shown: App.source.apiKeyShown
                }

                // Offline: where its songs come from.
                SettingsSection {
                    width: parent.width
                    shown: App.source.mode === "LocalOnly"
                    title: qsTr("This PC")

                    SettingsRow {
                        title: qsTr("Your music folder and downloads")
                        description: qsTr("Library shows both. Likes from your account carry over to downloaded songs.")

                        SettingsButton {
                            text: qsTr("Library settings")
                            onClicked: page.scrollTo(libraryGroup)
                        }
                    }
                }

                SettingsProfileCard {
                    width: parent.width
                    shown: App.settings.displayNameShown
                }
            }

            SettingsGroup {
                id: playbackGroup

                width: groups.width
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

                width: groups.width
                opacity: libraryInView.inView ? 1 : 0
                title: qsTr("Library and downloads")

                SettingsLibraryCard {
                    width: parent.width
                }

                SettingsDownloadsCard {
                    width: parent.width
                    shown: App.capabilities.downloadSongs
                }
            }

            SettingsGroup {
                id: lookGroup

                width: groups.width
                opacity: lookInView.inView ? 1 : 0
                title: qsTr("Look and language")

                SettingsAppearanceCard {
                    width: parent.width
                }

                SettingsLanguagesSection {
                    width: parent.width
                    shown: App.capabilities.chooseRegion
                }
            }

            SettingsGroup {
                id: connectionsGroup

                width: groups.width
                opacity: connectionsInView.inView ? 1 : 0
                title: qsTr("Connections")

                SettingsIntegrationsSection {
                    width: parent.width
                }
            }

            SettingsGroup {
                id: dataGroup

                width: groups.width
                opacity: dataInView.inView ? 1 : 0
                title: qsTr("Privacy and data")
                note: qsTr("Everything that removes something, in one place, mildest first.")

                SettingsDataSection {
                    width: parent.width
                }
            }

            SettingsGroup {
                id: aboutGroup

                width: groups.width
                opacity: aboutInView.inView ? 1 : 0
                title: qsTr("About")

                SettingsAboutCard {
                    width: parent.width
                }
            }
        }

        // A search that matches nothing.
        Text {
            width: column.width
            visible: SettingsStyle.needle !== "" && !groups.visibleChildren.some(item => item.height > 0)
            text: qsTr("No settings match “%1”.").arg(SettingsStyle.query.trim())
            font.family: SettingsStyle.family
            font.pixelSize: 14
            color: SettingsStyle.description
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
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
