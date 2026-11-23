import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// Languages as pills: the name, the native name in a softer colour, and a
// remove button on chosen languages that may be removed.
Flow {
    id: pills

    property var languages: []

    Layout.fillWidth: true
    Layout.leftMargin: 20
    Layout.rightMargin: 20
    spacing: 8

    Repeater {
        model: pills.languages

        delegate: Rectangle {
            id: pill

            required property var modelData

            implicitWidth: pillRow.implicitWidth + 24
            implicitHeight: 30
            radius: 15
            color: Theme.surface
            border.width: 1
            border.color: Theme.stroke

            Row {
                id: pillRow

                anchors.centerIn: parent
                spacing: 6

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: pill.modelData.name
                    font: Theme.chipLabel
                    color: Theme.fg
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: pill.modelData.nativeName !== ""
                    text: pill.modelData.nativeName
                    font: Theme.chipLabel
                    color: Theme.fgTertiary
                }

                IconButton {
                    Accessible.name: qsTr("Remove %1").arg(pill.modelData.name)
                    anchors.verticalCenter: parent.verticalCenter
                    visible: pill.modelData.removable
                    width: 20
                    height: 20
                    iconName: "close"
                    iconSize: 12
                    // Removing a chosen language is unticking it.
                    onClicked: App.regionLanguages.toggleLanguage(pill.modelData.code)
                }
            }
        }
    }
}
