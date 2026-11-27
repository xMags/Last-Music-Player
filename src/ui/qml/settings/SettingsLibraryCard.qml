pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// Music folder: the folder with Open and Change (a new folder is scanned at
// once); the file formats scanned, as pills, with Edit to tick them on and
// off; and Scan now, which turns into Cancel while a scan runs, with its
// progress under the row.
SettingsSection {
    id: section

    property bool editingFormats: false

    title: qsTr("Music folder")

    SettingsRow {
        title: qsTr("Folder")
        description: App.librarySettings.folder !== "" ? App.librarySettings.folder : qsTr("No folder selected")

        Row {
            spacing: 8

            SettingsButton {
                text: qsTr("Open")
                enabled: App.librarySettings.folder !== ""
                onClicked: App.librarySettings.openFolder()
            }

            SettingsButton {
                text: qsTr("Change")
                enabled: !App.librarySettings.scanning
                onClicked: App.librarySettings.changeFolder()
            }
        }
    }

    SettingsRow {
        title: qsTr("File formats")
        description: section.editingFormats ? qsTr("Tick the formats to include.") : qsTr("Included when scanning.")

        RowLayout {
            spacing: 16

            Row {
                visible: !section.editingFormats
                spacing: 6

                Repeater {
                    model: App.librarySettings.formats.filter(format => format.checked)

                    SettingsChip {
                        required property var modelData

                        text: modelData.label
                    }
                }
            }

            SettingsButton {
                text: section.editingFormats ? qsTr("Done") : qsTr("Edit")
                onClicked: section.editingFormats = !section.editingFormats
            }
        }
    }

    // Every format while editing: a pill is in when filled.
    SettingsBlock {
        shown: section.editingFormats
        joined: true

        Flow {
            width: parent.width
            spacing: 6

            Repeater {
                model: App.librarySettings.formats

                SettingsChip {
                    required property var modelData

                    toggles: true
                    text: modelData.label
                    selected: modelData.checked
                    Accessible.name: modelData.label
                    onClicked: App.librarySettings.toggleFormat(modelData.extension)
                }
            }
        }
    }

    SettingsRow {
        title: qsTr("Scan for new songs")
        description: App.librarySettings.scanStatus !== "" ? App.librarySettings.scanStatus
                                                           : qsTr("Finds songs added to the folder since the last scan.")

        // One of the two at a time; the Row sizes to it.
        Row {
            SettingsButton {
                visible: !App.librarySettings.scanning
                primary: true
                text: qsTr("Scan now")
                enabled: App.librarySettings.folder !== ""
                onClicked: App.librarySettings.scan()
            }

            SettingsButton {
                visible: App.librarySettings.scanning
                enabled: App.librarySettings.canCancel
                text: qsTr("Cancel")
                onClicked: App.librarySettings.cancelScan()
            }
        }
    }

    // A thin bar under the scan row while files are read.
    SettingsBlock {
        shown: App.librarySettings.scanning
        joined: true

        Rectangle {
            width: parent.width
            height: 4
            radius: 2
            color: SettingsStyle.controlBorder

            Rectangle {
                width: parent.width * Math.max(0, App.librarySettings.scanProgress)
                height: parent.height
                radius: parent.radius
                color: Theme.accentColor
            }
        }
    }
}
