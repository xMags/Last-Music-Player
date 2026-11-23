import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// About and data, from the current app: the app's mark, name and version
// with a count of the library, then Open data folder, the licences (not
// written yet, so the button waits) and Reset to defaults, which asks first.
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

    ConfirmDialog {
        id: resetDialog

        title: qsTr("Reset settings")
        description: qsTr("Restore all preferences to their defaults? Your music library and data are not affected.")
        confirmLabel: qsTr("Reset")
        onConfirmed: App.settings.resetDefaults()
    }
}
