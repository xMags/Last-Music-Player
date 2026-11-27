import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// About: the app's mark (a note on a 56px tile tinted by the accent), its
// name and version, then the licences (not written yet, so the button
// waits). What removes data is in Privacy and data.
SettingsSection {
    id: section

    SettingsBlock {
        keywords: AppInfo.displayName + "\n" + qsTr("Version") + "\n" + qsTr("About")

        RowLayout {
            width: parent.width
            spacing: 16

            Rectangle {
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56
                radius: 14
                color: SettingsStyle.chosenFill

                Icon {
                    anchors.centerIn: parent
                    name: "music-note"
                    size: 28
                    color: Theme.accentColor
                }
            }

            SettingsRowText {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                title: AppInfo.displayName
                titleSize: 16
                titleStrong: true
                description: qsTr("Version %1").arg(AppInfo.version)
            }
        }
    }

    SettingsRow {
        title: qsTr("Licenses and credits")
        description: qsTr("The open-source parts this app is built with")

        SettingsButton {
            text: qsTr("View")
            enabled: false
        }
    }
}
