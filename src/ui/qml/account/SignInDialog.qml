import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// The browser sign-in, as a dialog (auth-gate.ts and auth.css 307-510: the
// sleeve, its title, tagline, error box, the light button and the status
// line). The reference shows it as a full-window gate; this app also runs
// without an account, so here it opens from the Account card and can be
// cancelled. The button opens the browser; while the browser is open it
// reads "Open the browser again" and starts a fresh sign-in. A sign-in that
// succeeds switches to Account mode and closes the dialog.
T.Popup {
    id: dialog

    // The sleeve's fixed colours (auth.css 40-48).
    readonly property color sleeve: "#00505E"
    readonly property color ground: "#0E1416"
    readonly property color ink: "#F4F1EA"
    readonly property color inkSoft: "#DCEBEE"
    readonly property color label: "#CDE7EC"
    readonly property color link: "#A7EDFF"
    readonly property color accent: "#F0806A"

    readonly property bool waiting: App.source.signingIn

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(500, (parent ? parent.width : 500) - 48)
    implicitHeight: implicitContentHeight + topPadding + bottomPadding
    padding: 36
    modal: true
    focus: true
    closePolicy: T.Popup.CloseOnEscape

    onAboutToShow: App.source.clearSignInError()
    // Closing while the browser is open stops waiting for it.
    onClosed: {
        if (App.source.signingIn)
            App.source.cancelSignIn()
    }

    Connections {
        target: App.source

        function onSignInFinished() {
            dialog.close()
        }
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
    }

    background: Item {
        BoxShadow {
            anchors.fill: parent
            layers: Theme.shadowDialog
        }

        Rectangle {
            anchors.fill: parent
            color: dialog.sleeve

            // The 1px light edge along the top (inset 0 1px 0).
            Rectangle {
                width: parent.width
                height: 1
                color: Qt.rgba(1, 1, 1, 0.08)
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 4
            text: "LAST <font color=\"" + dialog.accent + "\">MUSIC</font>"
            textFormat: Text.StyledText
            font: Theme.signInTitle
            lineHeight: Theme.lineHeightSignInTitle
            lineHeightMode: Text.FixedHeight
            color: dialog.ink
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 28
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: 14
            text: qsTr("Sign in to use your account library on this PC. Your browser opens to complete sign-in.")
            font: Theme.signInTagline
            lineHeight: Theme.lineHeightSignInTagline
            lineHeightMode: Text.FixedHeight
            color: dialog.inkSoft
            wrapMode: Text.Wrap
        }

        // .auth-err
        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 18
            visible: App.source.signInError !== ""
            implicitHeight: errorRow.implicitHeight + 2 * 10
            radius: 6
            color: Qt.rgba(14 / 255, 20 / 255, 22 / 255, 0.5)

            RowLayout {
                id: errorRow

                x: 12
                y: 10
                width: parent.width - 2 * 12
                spacing: 10

                Icon {
                    Layout.alignment: Qt.AlignTop
                    Layout.topMargin: 1
                    name: "alert"
                    size: 16
                    color: "#FF9F87"
                }

                Text {
                    Layout.fillWidth: true
                    text: App.source.signInError
                    font: Theme.signInError
                    lineHeight: Theme.lineHeightSignInError
                    lineHeightMode: Text.FixedHeight
                    color: "#FFE1DA"
                    wrapMode: Text.Wrap
                }
            }
        }

        // .auth-btn
        T.Button {
            id: submit

            Layout.fillWidth: true
            Layout.topMargin: 30
            implicitHeight: 56
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            text: dialog.waiting ? qsTr("Open the browser again") : qsTr("Sign in")

            onClicked: App.source.beginSignIn()

            HoverHandler {
                cursorShape: Qt.PointingHandCursor
            }

            background: Rectangle {
                radius: 6
                color: submit.hovered ? "white" : dialog.ink

                FocusRing {
                    shown: submit.visualFocus
                    controlRadius: 6
                    offset: 3
                }
            }

            contentItem: Item {
                Text {
                    anchors.centerIn: parent
                    text: submit.text
                    font: Theme.signInButton
                    color: dialog.ground
                }

                Icon {
                    anchors.right: parent.right
                    anchors.rightMargin: 18
                    anchors.verticalCenter: parent.verticalCenter
                    name: "open"
                    size: 18
                    color: dialog.ground
                }
            }
        }

        // .auth-meta-foot: the status, and Cancel in the link colour.
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 20
            spacing: 8

            Rectangle {
                Layout.preferredWidth: 7
                Layout.preferredHeight: 7
                radius: 3.5
                color: dialog.waiting ? dialog.accent : dialog.label
            }

            Text {
                Layout.fillWidth: true
                text: dialog.waiting ? qsTr("WAITING ON BROWSER") : qsTr("READY")
                font: Theme.signInLabel
                color: dialog.label
            }

            T.Button {
                id: cancel

                implicitWidth: cancelLabel.implicitWidth
                implicitHeight: cancelLabel.implicitHeight
                padding: 0
                text: qsTr("CANCEL")
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                onClicked: dialog.close()

                HoverHandler {
                    cursorShape: Qt.PointingHandCursor
                }

                background: FocusRing {
                    shown: cancel.visualFocus
                    controlRadius: 2
                }

                contentItem: Text {
                    id: cancelLabel

                    text: cancel.text
                    font: Theme.signInLabel
                    color: dialog.link
                    opacity: cancel.hovered ? 1 : 0.85
                }
            }
        }
    }
}
