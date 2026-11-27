pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import LastMusicPlayer

// The account: a card labelled Account with one row, the 48px avatar (the
// picture, or the name's first letter on a grey disc), the name in semibold
// and "Signed in as @username" with the plan under it, and Sign out (asked
// first), a sign-out error under the row. Signed out, the same row says why
// and offers Sign in, or Cancel while the browser is open.
SettingsSection {
    id: card

    signal signInRequested()

    readonly property bool signedIn: App.source.signedIn

    title: qsTr("Account")

    SettingsBlock {
        keywords: qsTr("Account") + "\n" + qsTr("Sign out") + "\n" + qsTr("Sign in") + "\n" + App.profile.name

        RowLayout {
            width: parent.width
            height: Math.max(44, implicitHeight)
            spacing: 16

            Item {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48

                Rectangle {
                    anchors.fill: parent
                    radius: 24
                    color: SettingsStyle.quiet

                    Text {
                        anchors.centerIn: parent
                        visible: picture.status !== Image.Ready
                        text: card.signedIn ? App.profile.name.charAt(0).toUpperCase() : "?"
                        font.family: SettingsStyle.family
                        font.pixelSize: 18
                        font.weight: Font.Bold
                        color: SettingsStyle.title
                    }
                }

                // object-fit: cover, cut to the circle.
                Image {
                    id: picture

                    anchors.fill: parent
                    visible: status === Image.Ready
                    source: card.signedIn ? App.profile.avatarUrl : ""
                    sourceSize.width: 96
                    sourceSize.height: 96
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    layer.enabled: visible
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: circle
                        maskThresholdMin: 0.5
                        maskSpreadAtMin: 1.0
                    }
                }

                Rectangle {
                    id: circle

                    anchors.fill: parent
                    radius: width / 2
                    visible: false
                    layer.enabled: true
                }
            }

            SettingsRowText {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                title: card.signedIn ? App.profile.name : qsTr("Not signed in")
                titleStrong: true
                description: {
                    if (!card.signedIn)
                        return App.source.accountStatus
                    const parts = [App.source.accountUsername !== "" ? qsTr("Signed in as %1").arg(App.source.accountUsername)
                                                                     : qsTr("Signed in")]
                    if (App.profile.plan !== "")
                        parts.push(App.profile.plan)
                    return parts.join(" · ")
                }
                descriptionError: !card.signedIn && App.source.accountStatusError
            }

            SettingsButton {
                visible: card.signedIn
                enabled: !App.source.signingOut
                working: App.source.signingOut
                text: App.source.signingOut ? qsTr("Signing out…") : qsTr("Sign out")
                onClicked: signOutDialog.open()
            }

            SettingsButton {
                visible: !card.signedIn && !App.source.signingIn
                primary: true
                text: qsTr("Sign in")
                onClicked: card.signInRequested()
            }

            SettingsButton {
                visible: App.source.signingIn
                text: qsTr("Cancel")
                onClicked: App.source.cancelSignIn()
            }
        }
    }

    SettingsBlock {
        shown: card.signedIn && App.source.signOutError !== ""
        joined: true

        Text {
            width: parent.width
            text: App.source.signOutError
            font.family: SettingsStyle.family
            font.pixelSize: 12
            color: Theme.danger
            wrapMode: Text.Wrap
        }
    }

    SettingsConfirmDialog {
        id: signOutDialog

        title: qsTr("Sign out of %1?").arg(AppInfo.displayName)
        description: qsTr("Your synced library will require signing in again on this device.")
        confirmLabel: qsTr("Sign out")
        onConfirmed: App.source.signOut()
    }
}
