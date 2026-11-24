import QtQuick
import LastMusicPlayer

// A catalogue page's Back (.catalog-back, views.css 580-589): a chevron
// turned back and "Back" 6px after it, 12.5px 650 in the accent. The page
// sets the margins around it (16px above, 22px below).
Item {
    id: link

    property string text: qsTr("Back")

    signal clicked()

    implicitWidth: backRow.implicitWidth
    implicitHeight: backRow.implicitHeight
    activeFocusOnTab: true
    Accessible.role: Accessible.Link
    Accessible.name: link.text

    Keys.onReturnPressed: link.clicked()
    Keys.onSpacePressed: link.clicked()

    Row {
        id: backRow

        spacing: 6

        Icon {
            anchors.verticalCenter: parent.verticalCenter
            name: "chevron"
            size: 14
            rotation: 180
            color: Theme.accentColor
        }

        Text {
            text: link.text
            font: Theme.collectionBack
            color: Theme.accentColor
        }
    }

    HoverHandler { cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: link.clicked() }

    FocusRing {
        shown: link.activeFocus
        controlRadius: 2
    }
}
