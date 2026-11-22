pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// A dropdown list. Closed (.select, views.css 1769-1785): a 32px box with the
// chosen label and a chevron. Open (views.css 2689-2740): a flyout under the
// box drawn like the context menus, the chosen option semibold with an accent
// tick. `model` is a short list of labels.
T.ComboBox {
    id: control

    implicitWidth: Math.max(160, implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: 32
    // padding: 0 10px 0 12px; gap 8px before the chevron.
    leftPadding: 12
    rightPadding: 10 + 16 + 8
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : Theme.disabledOpacity
    font: Theme.selectLabel

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: Text {
        text: control.displayText
        font: control.font
        color: Theme.fg
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Icon {
        x: control.width - control.rightPadding + 8
        anchors.verticalCenter: parent.verticalCenter
        name: "chevron"
        size: 16
        color: Theme.fgTertiary
        // The chevron points down, and up while the list is open.
        rotation: control.popup.visible ? 270 : 90

        Behavior on rotation {
            enabled: !Motion.reduced
            NumberAnimation { duration: Motion.chevronTurn; easing: Motion.ease }
        }
    }

    background: ButtonSurface {
        implicitWidth: 160
        implicitHeight: 32
        radius: Theme.radiusMd
        color: control.enabled && control.hovered ? Theme.hover : Theme.surface
        border.width: 1
        border.color: Theme.strokeStrong
        rippleEnabled: control.enabled
        focusShown: control.visualFocus
    }

    delegate: T.ItemDelegate {
        id: option

        required property int index
        required property string modelData
        readonly property bool chosen: index === control.currentIndex

        width: ListView.view ? ListView.view.width : implicitWidth
        implicitHeight: 32
        // padding: 6px 10px; gap 10px before the tick.
        leftPadding: 10
        rightPadding: 10
        hoverEnabled: true
        highlighted: control.highlightedIndex === index
        text: modelData

        contentItem: Item {
            implicitWidth: optionLabel.implicitWidth + 10 + tick.implicitWidth

            Text {
                id: optionLabel

                anchors.left: parent.left
                anchors.right: tick.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: option.text
                font: option.chosen ? Theme.settingsButtonLabel : Theme.optionLabel
                color: option.enabled ? Theme.fg : Theme.fgDisabled
                elide: Text.ElideRight
            }

            Icon {
                id: tick

                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                name: "check"
                size: 14
                color: Theme.accentColor
                visible: option.chosen
            }
        }

        background: Rectangle {
            radius: 4
            color: option.enabled && (option.hovered || option.highlighted) ? Theme.hover : "transparent"
        }
    }

    popup: T.Popup {
        id: flyout

        y: control.height + 6
        width: Math.max(control.width, 168)
        implicitHeight: implicitContentHeight + topPadding + bottomPadding
        padding: 5
        transformOrigin: Item.Top

        enter: Transition {
            enabled: !Motion.reduced
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.popupOpen; easing: Motion.ease }
            NumberAnimation { property: "y"; from: control.height + 2; to: control.height + 6; duration: Motion.popupOpen; easing: Motion.ease }
        }
        exit: Transition {
            enabled: !Motion.reduced
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Motion.popupOpen; easing: Motion.ease }
        }

        contentItem: ListView {
            implicitHeight: contentHeight
            clip: true
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            T.ScrollBar.vertical: AppScrollBar {}
        }

        background: Item {
            BoxShadow {
                anchors.fill: parent
                layers: Theme.shadowFlyout
                radius: Theme.radiusMd
            }

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusMd
                color: Theme.surface
                border.width: 1
                border.color: Theme.stroke
            }
        }
    }
}
