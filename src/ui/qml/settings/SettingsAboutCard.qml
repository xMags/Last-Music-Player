import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// About: the app's mark, name and version with a count of the library, then
// the licences (not written yet, so the button waits). What removes data is
// in Privacy and data.
SettingsSection {
    id: section

    iconName: "info"

    Item {
        width: parent ? parent.width : 0
        implicitHeight: layout.implicitHeight + 2 * 20

        RowLayout {
            id: layout

            x: 20
            y: 20
            width: parent.width - 2 * 20
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
    }

    SettingsDivider {}

    SettingsRow {
        title: qsTr("Licenses and credits")
        description: qsTr("The open-source parts this app is built with")

        SettingsButton {
            text: qsTr("View")
            enabled: false
        }
    }
}
