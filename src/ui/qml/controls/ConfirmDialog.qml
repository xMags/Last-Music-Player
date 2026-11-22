import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// A centred question with Cancel and a confirm button, in the style of
// TextPromptDialog (modal.ts; styles.css 979-1027): title, description and
// the two buttons, Cancel first. Escape, Cancel and a press on the backdrop
// dismiss it; the confirm button reports `confirmed` and closes it.
T.Popup {
    id: dialog

    property string title: ""
    property string description: ""
    property string confirmLabel: qsTr("OK")

    signal confirmed()

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    // width: min(400px, 100%) inside 24px of padding; padding 22px 22px 18px.
    width: Math.min(400, (parent ? parent.width : 400) - 48)
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    topPadding: 22
    leftPadding: 22
    rightPadding: 22
    bottomPadding: 18
    modal: true
    focus: true
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

    onOpened: confirmButton.forceActiveFocus(Qt.TabFocusReason)

    T.Overlay.modal: Rectangle {
        color: Theme.scrim

        Behavior on opacity {
            enabled: !Motion.reduced
            NumberAnimation { duration: Motion.modal; easing: Motion.easeOut }
        }
    }

    enter: Transition {
        enabled: !Motion.reduced
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.modal; easing: Motion.standard }
        NumberAnimation { target: rise; property: "y"; from: 6; to: 0; duration: Motion.modal; easing: Motion.standard }
    }

    background: Item {
        transform: Translate { id: rise }

        BoxShadow {
            anchors.fill: parent
            layers: Theme.shadowDialog
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

    contentItem: Item {
        implicitWidth: layout.implicitWidth
        implicitHeight: layout.implicitHeight
        transform: Translate { y: rise.y }

        ColumnLayout {
            id: layout

            anchors.fill: parent
            spacing: 0

            Text {
                Layout.fillWidth: true
                text: dialog.title
                font: Theme.modalTitle
                lineHeight: Theme.lineHeightModalTitle
                lineHeightMode: Text.FixedHeight
                color: Theme.fg
                wrapMode: Text.Wrap
            }

            Text {
                Layout.fillWidth: true
                Layout.topMargin: 6
                visible: dialog.description !== ""
                text: dialog.description
                font: Theme.modalDescription
                lineHeight: Theme.lineHeightModalDescription
                lineHeightMode: Text.FixedHeight
                color: Theme.fgSecondary
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.topMargin: 20
                Layout.alignment: Qt.AlignRight
                spacing: 10

                AppButton {
                    Layout.minimumWidth: 86
                    text: qsTr("Cancel")
                    onClicked: dialog.close()
                }

                AppButton {
                    id: confirmButton

                    Layout.minimumWidth: 86
                    variant: AppButton.Accent
                    text: dialog.confirmLabel
                    onClicked: {
                        dialog.close()
                        dialog.confirmed()
                    }
                }
            }
        }
    }
}
