import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// The accent text link of a section header (.section-link, styles.css
// 503-511): 12px semibold in the accent colour.
T.AbstractButton {
    id: link

    implicitWidth: label.implicitWidth
    implicitHeight: label.implicitHeight
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: Text {
        id: label

        text: link.text
        font: Theme.sectionLink
        color: Theme.accentColor
        opacity: link.hovered ? 0.8 : 1
    }

    background: FocusRing {
        shown: link.visualFocus
        controlRadius: 2
    }
}
