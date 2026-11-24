import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The heading of a shelf or a section (.section-header, styles.css 476-511):
// the 20px title, then at the far right an accent link such as "See all".
// Extra controls (a List / Grid switch) go in as children, at the right.
RowLayout {
    id: header

    property string title: ""
    // .section-pill: a small label right after the title (catalogue pages).
    property string pill: ""
    property string linkText: ""
    default property alias trailing: trailingSlot.data

    signal linkClicked()

    spacing: 14

    Text {
        // Whole pixels, rounded up: the layout snaps widths down, and a title
        // a fraction of a pixel short of its text would elide ("Your mix…").
        Layout.minimumWidth: 0
        Layout.preferredWidth: Math.ceil(implicitWidth)
        Layout.maximumWidth: Math.ceil(implicitWidth)
        Layout.fillWidth: true
        text: header.title
        font: Theme.sectionTitle
        lineHeight: Theme.lineHeightSectionTitle
        lineHeightMode: Text.FixedHeight
        color: Theme.fg
        elide: Text.ElideRight
    }

    // .section-pill: 10px 600, 0.04em, upper case, the secondary colour on
    // the neutral fill with a hairline, padded 3px 9px, fully round.
    Rectangle {
        Layout.alignment: Qt.AlignVCenter
        visible: header.pill !== ""
        implicitWidth: pillText.implicitWidth + 2 * 9
        implicitHeight: pillText.implicitHeight + 2 * 3
        radius: height / 2
        color: Theme.neutral
        border.width: 1
        border.color: Theme.stroke

        Text {
            id: pillText

            anchors.centerIn: parent
            text: header.pill.toUpperCase()
            font: Theme.sectionPill
            lineHeight: Theme.lineHeightSectionPill
            lineHeightMode: Text.FixedHeight
            color: Theme.fgSecondary
        }
    }

    // margin-left: auto on the link: everything else stays by the title.
    Item {
        Layout.fillWidth: true
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
