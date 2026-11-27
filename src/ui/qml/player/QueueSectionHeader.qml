import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// The Up Next header (.queue-header, styles.css 1270-1276 and 1896-1914):
// "Next in queue" (with its hairline in the rail, plain in the full-screen
// panel), "Autoplay off" in the accent colour while related songs will not
// top the queue up, and, while songs are queued, the red "Clear", which asks
// first.
RowLayout {
    id: header

    property bool rule: true
    property color labelColor: Theme.fgTertiary

    spacing: 8

    // It takes the free width with or without its hairline, so the status
    // and Clear always sit at the right end.
    RailSectionLabel {
        Layout.fillWidth: true
        Layout.maximumWidth: Number.POSITIVE_INFINITY
        Layout.rightMargin: header.rule ? 12 : 0
        text: qsTr("NEXT IN QUEUE")
        rule: header.rule
        textColor: header.labelColor
    }

    AppText {
        visible: App.queueRail.autoplayStatus !== ""
        text: App.queueRail.autoplayStatus
        font: Theme.queueStatus
        color: Theme.accentColor
    }

    T.AbstractButton {
        id: clear

        visible: App.queueRail.queue.count > 0
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        implicitHeight: implicitContentHeight + topPadding + bottomPadding
        padding: 0
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: qsTr("Clear the queue")
        // Clears at once, as the reference does (only its phone sheet asks).
        onClicked: App.queueRail.clearUpcoming()

        contentItem: AppText {
            id: clearLabel

            text: qsTr("Clear")
            font: Theme.queueAction
            color: clear.hovered ? Theme.queueClearHover : Theme.queueClear

            // :hover underlines it, 3px below the text.
            Rectangle {
                y: clearLabel.baselineOffset + 3
                width: clearLabel.width
                height: 1
                visible: clear.hovered
                color: clearLabel.color
            }
        }

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }

        FocusRing {
            shown: clear.visualFocus
            controlRadius: 2
        }
    }
}
