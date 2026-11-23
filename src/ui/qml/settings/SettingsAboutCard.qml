import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// About and data, from the current app: the app's mark, name and version
// with a count of the library, then Open data folder, the licences (not
// written yet, so the button waits) and Reset to defaults, which asks first;
// then "Delete all data" (the current app's clean-up), which says what it
// removes and asks for CLEAN UP to be typed. Music files are never deleted.
SettingsSection {
    id: section

    iconName: "info"
    title: qsTr("About and data")

    Item {
        width: parent ? parent.width : 0
        implicitHeight: layout.implicitHeight + 2 * 20

        ColumnLayout {
            id: layout

            x: 20
            y: 20
            width: parent.width - 2 * 20
            spacing: 16

            RowLayout {
                spacing: 14

                // The app's mark: a note on the teal brand gradient.
                Rectangle {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 48
                    radius: 12
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: Theme.brandLogoStart }
                        GradientStop { position: 1; color: Theme.brandLogoEnd }
                    }

                    Icon {
                        anchors.centerIn: parent
                        name: "music-note"
                        size: 24
                        color: "white"
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Text {
                        Layout.fillWidth: true
                        text: App.settingsAbout.title
                        font: Theme.settingsRowTitle
                        color: Theme.fg
                        elide: Text.ElideRight
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: App.settingsAbout.libraryLine
                        font: Theme.settingsRowDescription
                        color: Theme.fgTertiary
                        elide: Text.ElideRight
                    }
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: 10

                AppButton {
                    text: qsTr("Open data folder")
                    iconName: "open"
                    onClicked: App.settingsAbout.openDataFolder()
                }

                AppButton {
                    text: qsTr("Licenses and credits")
                    enabled: false
                }

                AppButton {
                    text: qsTr("Reset to defaults")
                    onClicked: resetDialog.open()
                }
            }
        }
    }

    SettingsRow {
        title: qsTr("Delete all data")
        description: App.dataSettings.status !== "" ? App.dataSettings.status
            : qsTr("Clears the library, history, likes, playlists, queues, settings, sign-in and caches.")

        AppButton {
            text: App.dataSettings.busy ? qsTr("Cleaning up…") : qsTr("Clean up…")
            variant: AppButton.Danger
            enabled: !App.dataSettings.busy
            onClicked: {
                cleanupDialog.text = ""
                cleanupDialog.errorText = ""
                cleanupDialog.open()
            }
        }
    }

    TextPromptDialog {
        id: cleanupDialog

        title: qsTr("Clean up everything?")
        description: qsTr("This clears the local library database, playback history, liked tracks, playlists, queues, settings, provider config and app caches. Music files on disk are not deleted.")
        fieldLabel: qsTr("Type CLEAN UP to confirm")
        placeholderText: "CLEAN UP"
        submitLabel: qsTr("Clean up")
        onSubmitted: value => {
            if (App.dataSettings.deleteAll(value))
                cleanupDialog.finish()
            else
                cleanupDialog.errorText = qsTr("Type CLEAN UP in capitals to confirm.")
        }
    }

    ConfirmDialog {
        id: resetDialog

        title: qsTr("Reset settings")
        description: qsTr("Restore all preferences to their defaults? Your music library and data are not affected.")
        confirmLabel: qsTr("Reset")
        onConfirmed: App.settings.resetDefaults()
    }
}
