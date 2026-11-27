pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// Library, from the current app (MainWindow.xaml 1786-1852): the music folder
// with Change, Open and Scan now; while a scan runs, its progress and Cancel;
// then the file format chips. A new folder is scanned at once.
SettingsSection {
    id: section

    iconName: "library"
    title: qsTr("Music folder")

    SettingsRow {
        title: qsTr("Music folder")
        description: App.librarySettings.folder !== "" ? App.librarySettings.folder : qsTr("No folder selected")

        Row {
            spacing: 8

            AppButton {
                text: qsTr("Change")
                enabled: !App.librarySettings.scanning
                onClicked: App.librarySettings.changeFolder()
            }

            AppButton {
                text: qsTr("Open")
                enabled: App.librarySettings.folder !== ""
                onClicked: App.librarySettings.openFolder()
            }

            AppButton {
                text: qsTr("Scan now")
                iconName: "history"
                variant: AppButton.Accent
                enabled: App.librarySettings.folder !== "" && !App.librarySettings.scanning
                onClicked: App.librarySettings.scan()
            }
        }
    }

    // The scan's progress, or how the last one ended.
    Item {
        width: parent ? parent.width : 0
        visible: App.librarySettings.scanStatus !== ""
        implicitHeight: visible ? status.implicitHeight + 2 * 12 : 0

        ColumnLayout {
            id: status

            x: 20
            y: 12
            width: parent.width - 2 * 20
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Text {
                    Layout.fillWidth: true
                    text: App.librarySettings.scanStatus
                    font: Theme.settingsRowDescription
                    color: Theme.fgSecondary
                    elide: Text.ElideRight
                }

                AppButton {
                    visible: App.librarySettings.scanning
                    enabled: App.librarySettings.canCancel
                    text: qsTr("Cancel")
                    onClicked: App.librarySettings.cancelScan()
                }
            }

            // A thin bar under the status while files are read.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 3
                visible: App.librarySettings.scanning
                radius: 1.5
                color: Theme.sliderTrack

                Rectangle {
                    width: parent.width * Math.max(0, App.librarySettings.scanProgress)
                    height: parent.height
                    radius: parent.radius
                    color: Theme.accentColor
                }
            }
        }
    }

    SettingsDivider {}

    Item {
        width: parent ? parent.width : 0
        implicitHeight: formats.implicitHeight + 2 * 14

        ColumnLayout {
            id: formats

            x: 20
            y: 14
            width: parent.width - 2 * 20
            spacing: 10

            ColumnLayout {
                spacing: 3

                Text {
                    text: qsTr("File formats")
                    font: Theme.settingsRowTitle
                    color: Theme.fg
                }

                Text {
                    text: qsTr("Included when scanning.")
                    font: Theme.settingsRowDescription
                    color: Theme.fgTertiary
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: 8

                Repeater {
                    model: App.librarySettings.formats

                    Chip {
                        required property var modelData

                        text: modelData.label
                        selected: modelData.checked
                        onClicked: App.librarySettings.toggleFormat(modelData.extension)
                    }
                }
            }
        }
    }
}
