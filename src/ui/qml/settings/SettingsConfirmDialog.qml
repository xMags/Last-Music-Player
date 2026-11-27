import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// A question Settings asks before something that cannot be undone
// (.settings-confirm-dialog, views.css 1630-1700): the title and what will
// happen, then a strip on the page colour with the danger button and Cancel,
// side by side and equally wide. Escape, Cancel and a press outside dismiss
// it; the danger button closes it and reports `confirmed`.
T.Popup {
    id: dialog

    property string title: ""
    property string description: ""
    property string confirmLabel: ""

    signal confirmed()

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    // width: min(calc(100% - 32px), 440px)
    width: Math.min(440, (parent ? parent.width : 440) - 32)
    implicitHeight: implicitContentHeight
    padding: 0
    modal: true
    focus: true
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

    // A modal dialog focuses its first control, the danger button.
    onOpened: confirmButton.forceActiveFocus(Qt.TabFocusReason)

    // ::backdrop: rgba(0, 0, 0, .45) with a 4px blur.
    T.Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.45)

        BackdropBlur {
            z: -1
            source: App.effects.windowBackdrop
            blurPx: 4
        }
    }

    // settings-dialog-in: from opacity 0, 6px lower and 98.5% the size.
    enter: Transition {
        enabled: !Motion.reduced
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Motion.settingsDialogIn; easing: Motion.ease }
        NumberAnimation { target: rise; property: "y"; from: 6; to: 0; duration: Motion.settingsDialogIn; easing: Motion.ease }
        NumberAnimation { target: grow; property: "xScale"; from: 0.985; to: 1; duration: Motion.settingsDialogIn; easing: Motion.ease }
        NumberAnimation { target: grow; property: "yScale"; from: 0.985; to: 1; duration: Motion.settingsDialogIn; easing: Motion.ease }
    }

    background: Item {
        transform: [
            Scale { id: grow; origin.x: dialog.width / 2; origin.y: dialog.height / 2 },
            Translate { id: rise }
        ]

        BoxShadow {
            anchors.fill: parent
            layers: Theme.shadowFlyout
            radius: Theme.radiusLg
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLg
            color: Theme.surfaceElev
            border.width: 1
            border.color: Theme.strokeStrong
        }
    }

    contentItem: Item {
        implicitHeight: body.y + body.implicitHeight + actions.implicitHeight
        transform: [
            Scale { origin.x: dialog.width / 2; origin.y: dialog.height / 2; xScale: grow.xScale; yScale: grow.yScale },
            Translate { y: rise.y }
        ]

        // .settings-confirm-body: padding 24px 24px 22px.
        ColumnLayout {
            id: body

            x: 24
            y: 24
            width: parent.width - 48
            spacing: 10

            AppText {
                Layout.fillWidth: true
                text: dialog.title
                font: Theme.modalTitle
                color: Theme.fg
                wrapMode: Text.Wrap
            }

            AppText {
                Layout.fillWidth: true
                visible: dialog.description !== ""
                text: dialog.description
                font: Theme.settingsDialogText
                lineHeight: Theme.lineHeightSettingsDialogText
                lineHeightMode: Text.FixedHeight
                color: Theme.fgSecondary
                wrapMode: Text.Wrap
            }

            // The 22px under the text.
            Item {
                Layout.preferredHeight: 22 - body.spacing
            }
        }

        // .settings-dialog-actions here: two equal columns 8px apart, padded
        // 18px 24px, a divider on top, the page colour behind; the inner
        // corners follow the card's.
        Rectangle {
            id: actions

            y: body.y + body.implicitHeight
            width: parent.width
            implicitHeight: buttons.implicitHeight + 36
            height: implicitHeight
            color: Theme.bg
            bottomLeftRadius: Theme.radiusLg - 1
            bottomRightRadius: Theme.radiusLg - 1

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.divider
            }

            RowLayout {
                id: buttons

                x: 24
                y: 18
                width: parent.width - 48
                spacing: 8

                T.Button {
                    id: confirmButton

                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: 34
                    leftPadding: 18
                    rightPadding: 18
                    hoverEnabled: true
                    focusPolicy: Qt.StrongFocus
                    font: Theme.body
                    text: dialog.confirmLabel
                    onClicked: {
                        dialog.close()
                        dialog.confirmed()
                    }

                    HoverHandler {
                        cursorShape: Qt.PointingHandCursor
                    }

                    contentItem: AppText {
                        text: confirmButton.text
                        font: confirmButton.font
                        color: Theme.dangerConfirmInk
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }

                    background: ButtonSurface {
                        implicitHeight: 34
                        radius: Theme.radiusSm
                        color: confirmButton.hovered ? Theme.dangerConfirmHover : Theme.danger
                        fadeDuration: 0
                        focusShown: confirmButton.visualFocus
                    }
                }

                SettingsButton {
                    id: cancelButton

                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: 34
                    font: Theme.body
                    text: qsTr("Cancel")
                    onClicked: dialog.close()
                }
            }
        }
    }
}
