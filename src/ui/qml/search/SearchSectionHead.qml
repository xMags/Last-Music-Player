import QtQuick
import LastMusicPlayer

// A results section's heading (.search-section-head, views.css 1905-1926):
// the name in small spaced capitals, a line to the right, then the count;
// a link (Clear) can stand at the end instead.
Item {
    id: head

    property string text: ""
    property int count: -1
    property string linkText: ""

    signal linkClicked()

    implicitHeight: label.implicitHeight + 10

    Text {
        id: label

        text: head.text
        font: Theme.eyebrow
        color: Theme.fgTertiary
    }

    Rectangle {
        x: label.width + 12
        anchors.verticalCenter: label.verticalCenter
        width: Math.max(0, head.width - x - (tail.visible ? tail.width + 12 : 0))
        height: 1
        color: Theme.stroke
    }

    Item {
        id: tail

        anchors.right: parent.right
        anchors.verticalCenter: label.verticalCenter
        visible: head.count >= 0 || head.linkText !== ""
        width: head.linkText !== "" ? link.implicitWidth : countText.implicitWidth
        height: label.height

        Text {
            id: countText

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: head.linkText === "" && head.count >= 0
            text: head.count === 1 ? qsTr("1 result") : qsTr("%1 results").arg(head.count)
            font: Theme.cardSubtitle
            color: Theme.fgTertiary
        }

        LinkButton {
            id: link

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: head.linkText !== ""
            text: head.linkText
            onClicked: head.linkClicked()
        }
    }
}
