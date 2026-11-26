import QtQuick
import LastMusicPlayer

// A tier's label on Home: small capitals with a line to the right.
// .home-tier's 52px top margin collapses with the 32px every section ends
// with, so 20px more here; the hero ends with its own 44px (HomeSection).
Item {
    id: label

    property string text: ""
    property real below: 18

    width: parent ? parent.width : 0
    height: text === "" ? 0 : 20 + tierText.implicitHeight + below
    visible: text !== ""

    Text {
        id: tierText

        y: 20
        text: label.text
        font: Theme.homeLabel
        color: Theme.fgSecondary
    }

    Rectangle {
        x: tierText.width + 14
        anchors.verticalCenter: tierText.verticalCenter
        width: label.width - x
        height: 1
        color: Theme.stroke
    }
}
