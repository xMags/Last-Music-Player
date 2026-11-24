import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// The accent text link of a section header (.section-link, styles.css
// 503-511): 12px semibold in the accent colour.
T.AbstractButton {
    id: link

    // An inline .button-link takes the text around it instead.
    property font labelFont: Theme.sectionLink
    property color labelColor: Theme.accentColor

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
        font: link.labelFont
        color: link.labelColor
    }

    background: FocusRing {
        shown: link.visualFocus
        controlRadius: 2
    }
}
