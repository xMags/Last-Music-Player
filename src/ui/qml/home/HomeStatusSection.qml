pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// Home's catalogue status: placeholder shelves while the catalogue loads, a
// notice with "Try again" when it could not, or a quiet line when the charts
// shown are cached.
Column {
    id: status

    spacing: 12
    bottomPadding: 32

    // Nothing to show yet: three shelves of placeholders.
    Repeater {
        model: App.home.catalogSkeleton ? 3 : 0

        Column {
            width: status.width
            spacing: 14

            Skeleton {
                width: 180
                height: 20
            }

            SkeletonGrid {
                width: status.width
                count: 6
            }
        }
    }

    // homeNotice: a quiet card with an alert, the message and a "Try again"
    // pill (styles.css 2925-2953).
    Rectangle {
        visible: !App.home.catalogSkeleton && App.home.catalogError !== ""
        width: Math.min(640, status.width)
        implicitHeight: noticeRow.implicitHeight + 2 * 14
        radius: 12
        color: Theme.surface
        border.width: 1
        border.color: Theme.stroke

        RowLayout {
            id: noticeRow

            x: 16
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 2 * 16
            spacing: 12

            Icon {
                name: "alert"
                size: 16
                color: Theme.fgTertiary
            }

            Text {
                Layout.fillWidth: true
                text: App.home.catalogError
                font: Theme.viewSubtitle
                color: Theme.fgSecondary
                wrapMode: Text.Wrap
            }

            NoticeButton {
                text: qsTr("Try again")
                onClicked: App.home.retryCatalog()
            }
        }
    }

    // .home-refresh-status: cached charts, 11px tertiary, with an inline
    // Refresh.
    Row {
        visible: !App.home.catalogSkeleton && App.home.catalogError === "" && App.home.catalogCached
        topPadding: 7
        spacing: 0

        Text {
            text: qsTr("Showing cached charts. ")
            font: Theme.refreshStatus
            color: Theme.fgTertiary
        }

        LinkButton {
            text: qsTr("Refresh")
            labelFont: Theme.refreshStatus
            labelColor: Theme.fg
            onClicked: App.home.retryCatalog()
        }
    }

    // .home-notice-button: a 32px outlined pill, padded 0 14px, 13px 600;
    // the hover colour on hover.
    component NoticeButton: T.Button {
        id: noticeButton

        implicitHeight: 32
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        leftPadding: 14
        rightPadding: 14
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        font: Theme.settingsButtonLabel

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }

        contentItem: Text {
            text: noticeButton.text
            font: noticeButton.font
            color: Theme.fg
            verticalAlignment: Text.AlignVCenter
        }

        background: ButtonSurface {
            radius: height / 2
            border.width: 1
            border.color: Theme.stroke
            color: noticeButton.hovered ? Theme.hover : "transparent"
            focusShown: noticeButton.visualFocus
        }
    }
}
