import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// One setting on one line: title and description on the left, the control
// on the right, 16px between them; padded 14px by 20px round a body at
// least 44px tall, under the card's 1px line. The control is the row's
// child. `shown` is the row's own condition; a search may hide it too.
Item {
    id: row

    property string title: ""
    property string description: ""
    // The description in the danger colour, for a failed load or save.
    property bool descriptionError: false
    // The title in the danger colour.
    property bool titleDanger: false
    property bool shown: true
    default property alias control: controlSlot.data

    width: parent ? parent.width : 0
    visible: shown && search.matches
    implicitHeight: 1 + 14 + Math.max(44, layout.implicitHeight) + 14

    SettingsSearchable {
        id: search

        row: row
        searchText: row.title + "\n" + row.description
    }

    RowLayout {
        id: layout

        x: 20
        y: 15
        width: parent.width - 2 * 20
        height: Math.max(44, implicitHeight)
        spacing: 16

        SettingsRowText {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            title: row.title
            description: row.description
            titleDanger: row.titleDanger
            descriptionError: row.descriptionError
        }

        Item {
            id: controlSlot

            Layout.alignment: Qt.AlignVCenter
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
        }
    }
}
