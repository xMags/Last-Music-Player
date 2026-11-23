import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The heading of a shelf or a section (.section-header, styles.css 476-511):
// the 20px title, then at the far right an accent link such as "See all".
// Extra controls (a List / Grid switch) go in as children, at the right.
RowLayout {
    id: header

    property string title: ""
    property string linkText: ""
    default property alias trailing: trailingSlot.data

    signal linkClicked()

    spacing: 14

    Text {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        text: header.title
        font: Theme.sectionTitle
        lineHeight: Theme.lineHeightSectionTitle
        lineHeightMode: Text.FixedHeight
        color: Theme.fg
        elide: Text.ElideRight
    }

    Row {
        id: trailingSlot

        Layout.alignment: Qt.AlignVCenter
        spacing: 10
    }

    LinkButton {
        Layout.alignment: Qt.AlignVCenter
        visible: header.linkText !== ""
        text: header.linkText
        onClicked: header.linkClicked()
    }
}
