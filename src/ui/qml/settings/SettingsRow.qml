import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// One setting on one line (.settings-row, views.css 1150-1166): title and
// description on the left, the control on the right; at least 64px tall,
// padded 12px by 20px, 16px between text and control. The control is the
// row's child.
Item {
    id: row

    property string title: ""
    property string description: ""
    // The description in the danger colour, for a failed load or save.
    property bool descriptionError: false
    default property alias control: controlSlot.data

    width: parent ? parent.width : 0
    implicitHeight: Math.max(64, layout.implicitHeight + 2 * 12)

    RowLayout {
        id: layout

        anchors.fill: parent
        anchors.leftMargin: 20
        anchors.rightMargin: 20
        spacing: 16

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 3

            Text {
                Layout.fillWidth: true
                text: row.title
                font: Theme.settingsRowTitle
                lineHeight: Theme.lineHeightSettingsRowTitle
                lineHeightMode: Text.FixedHeight
                color: Theme.fg
                elide: Text.ElideRight
            }

            Text {
                Layout.fillWidth: true
                visible: row.description !== ""
                text: row.description
                font: Theme.settingsRowDescription
                lineHeight: Theme.lineHeightSettingsRowDescription
                lineHeightMode: Text.FixedHeight
                color: row.descriptionError ? Theme.danger : Theme.fgTertiary
                wrapMode: Text.Wrap
            }
        }

        Item {
            id: controlSlot

            Layout.alignment: Qt.AlignVCenter
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
        }
    }
}
