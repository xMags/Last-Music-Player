pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import LastMusicPlayer

// The account (views/settings.ts render; views.css 1252-1289): a card
// labelled Account, the avatar in an accent ring, the
// name and the plan, Sign out (asked first), and a sign-out error under it.
// Signed out, which the reference never shows, the same card says why and
// offers Sign in, or Cancel while the browser is open. The music region is
// with the languages (SettingsLanguagesSection).
SettingsSection {
    id: card

    title: qsTr("Account")

    signal signInRequested()

    readonly property bool signedIn: App.source.signedIn

    // .settings-hero-main: 20px padding, 16px between the parts.
    Item {
        width: parent ? parent.width : 0
        implicitHeight: main.implicitHeight + 2 * 20

        RowLayout {
            id: main

            x: 20
            y: 20
            width: parent.width - 2 * 20
            spacing: 16

            // .settings-avatar: a 2px ring of the accent fading out at
            // 135 degrees, then a 2px surface border round the picture or
            // the name's first letter.
            Item {
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56

                // linear-gradient(135deg, ...) on the 56px square runs along
                // its 79px diagonal, so across the circle it goes from 14.6%
                // to 85.4% of the way: the circle turned 45 degrees with a
                // horizontal gradient between those two mixes is the same.
                Rectangle {
                    id: ring

                    readonly property color from: Theme.accentColor
                    readonly property color to: Theme.mixSrgb(Theme.accentFill, "transparent", 0.25)

                    anchors.fill: parent
                    radius: width / 2
                    rotation: 45
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: Theme.mixSrgb(ring.from, ring.to, 0.8536) }
                        GradientStop { position: 1; color: Theme.mixSrgb(ring.from, ring.to, 0.1464) }
                    }
                }

                Rectangle {
                    id: face

                    anchors.fill: parent
                    anchors.margins: 2
                    radius: width / 2
                    color: Theme.neutral
                    border.width: 2
                    border.color: Theme.surface

                    Text {
                        anchors.centerIn: parent
                        visible: picture.status !== Image.Ready
                        text: App.profile.name.charAt(0).toUpperCase()
                        font: Theme.settingsAvatarLetter
                        color: Theme.fg
                    }

                    // object-fit: cover, cut to the circle inside the border.
                    Image {
                        id: picture

                        anchors.fill: parent
                        anchors.margins: 2
                        visible: status === Image.Ready
                        source: card.signedIn ? App.profile.avatarUrl : ""
                        sourceSize.width: 104
                        sourceSize.height: 104
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

                        anchors.fill: picture
                        radius: width / 2
                        visible: false
                        layer.enabled: true
                    }
                }
            }

            // .settings-hero-copy: the name, then the plan, 3px apart.
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                Text {
                    Layout.fillWidth: true
                    text: card.signedIn ? App.profile.name : qsTr("Not signed in")
                    font: Theme.settingsHeroName
                    lineHeight: Theme.lineHeightSettingsHeroName
                    lineHeightMode: Text.FixedHeight
                    color: Theme.fg
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    visible: card.signedIn && App.profile.plan !== ""
                    text: App.profile.plan
                    font: Theme.settingsTier
                    lineHeight: Theme.lineHeightSettingsTier
                    lineHeightMode: Text.FixedHeight
                    color: Theme.accentColor
                    elide: Text.ElideRight
                }

                // Signed out: why, in the row description style.
                Text {
                    Layout.fillWidth: true
                    visible: !card.signedIn && App.source.accountStatus !== ""
                    text: App.source.accountStatus
                    font: Theme.settingsRowDescription
                    lineHeight: Theme.lineHeightSettingsRowDescription
                    lineHeightMode: Text.FixedHeight
                    color: App.source.accountStatusError ? Theme.danger : Theme.fgTertiary
                    wrapMode: Text.Wrap
                }
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

    // .settings-hero-error: margin -8px 20px 14px, 12px, the danger colour.
    Item {
        width: parent ? parent.width : 0
        visible: card.signedIn && App.source.signOutError !== ""
        implicitHeight: visible ? heroError.implicitHeight - 8 + 14 : 0

        Text {
            id: heroError

            x: 20
            y: -8
            width: parent.width - 2 * 20
            text: App.source.signOutError
            font: Theme.settingsRowDescription
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
