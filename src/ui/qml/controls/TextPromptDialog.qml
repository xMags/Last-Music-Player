import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// A centred dialog with one text field (modal.ts; styles.css 979-1027):
// title, optional description, a labelled field, an inline error and Cancel /
// confirm buttons. Escape, Cancel and a press on the backdrop dismiss it unless
// `busy` is set while the owner works on the submitted value. On success the
// owner calls finish(); on failure it clears `busy` and sets `errorText`.
// Any other way of closing reports `cancelled`.
T.Popup {
    id: dialog

    property string title: ""
    property string description: ""
    property string fieldLabel: ""
    property string placeholderText: ""
    property alias text: field.text
    property string submitLabel: qsTr("OK")
    property string busyLabel: ""
    property int maximumLength: 32767
    property bool busy: false
    property string errorText: ""
    property bool finished: false

    signal submitted(string value)
    signal cancelled()

    function finish() {
        busy = false
        finished = true
        close()
    }

    function submit() {
        if (busy)
            return
        errorText = ""
        submitted(field.text.trim())
    }

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
    closePolicy: busy ? T.Popup.NoAutoClose : T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

    onOpened: {
        finished = false
        errorText = ""
        field.forceActiveFocus()
        field.selectAll()
    }
    onClosed: {
        if (!finished)
            cancelled()
    }

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

            Text {
                Layout.fillWidth: true
                Layout.topMargin: 16
                Layout.bottomMargin: 7
                text: dialog.fieldLabel
                font: Theme.fieldLabel
                color: Theme.fgTertiary
            }

            PromptTextField {
                id: field

                Layout.fillWidth: true
                placeholderText: dialog.placeholderText
                maximumLength: dialog.maximumLength
                enabled: !dialog.busy
                onAccepted: dialog.submit()
            }

            Text {
                Layout.fillWidth: true
                Layout.topMargin: 10
                visible: dialog.errorText !== ""
                text: dialog.errorText
                font: Theme.errorText
                color: Theme.danger
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.topMargin: 20
                Layout.alignment: Qt.AlignRight
                spacing: 10

                AppButton {
                    Layout.minimumWidth: 86
                    text: qsTr("Cancel")
                    enabled: !dialog.busy
                    onClicked: dialog.close()
                }

                AppButton {
                    Layout.minimumWidth: 86
                    variant: AppButton.Accent
                    text: dialog.busy && dialog.busyLabel !== "" ? dialog.busyLabel : dialog.submitLabel
                    enabled: !dialog.busy
                    onClicked: dialog.submit()
                }
            }
        }
    }
}
