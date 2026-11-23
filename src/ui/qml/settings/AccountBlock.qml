import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The Account source's connection: the account hero (.settings-hero,
// views.css 1252-1289: a 56px avatar in an accent ring, the name, then
// "@name · plan" in the tier style) with Manage account and Sign out, and a
// status row. Signed out it offers Sign in; while the browser is open it
// offers Cancel. Sign out asks first, as the reference does.
SettingsSection {
    id: section

    iconName: "person"
    title: qsTr("Account connection")
    note: App.source.signedIn ? qsTr("Signed in") : qsTr("Not connected")

    signal signInRequested()

    Item {
        width: parent ? parent.width : 0
        implicitHeight: hero.implicitHeight + 2 * 20

        RowLayout {
            id: hero

            x: 20
            y: 20
            width: parent.width - 2 * 20
            spacing: 16

            // The avatar: the name's first letter in a ring of the accent.
            Rectangle {
                Layout.preferredWidth: 56
                Layout.preferredHeight: 56
                radius: 28
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Theme.accentColor }
                    GradientStop { position: 1; color: Theme.mixSrgb(Theme.accentFill, "transparent", 0.25) }
                }

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: width / 2
                    color: Theme.neutral
                    border.width: 2
                    border.color: Theme.surface

                    Text {
                        anchors.centerIn: parent
                        text: App.source.accountName.charAt(0).toUpperCase()
                        font: Theme.settingsSection
                        color: Theme.fg
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3

                Text {
                    Layout.fillWidth: true
                    text: App.source.signedIn ? App.source.accountName : qsTr("Not signed in")
                    font: Theme.settingsSection
                    color: Theme.fg
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    visible: App.source.signedIn
                    text: App.source.accountUsername !== ""
                        ? App.source.accountUsername + " · " + App.source.accountPlan
                        : App.source.accountPlan
                    font: Theme.profileTier
                    color: Theme.accentColor
                    elide: Text.ElideRight
                }
            }

            AppButton {
                visible: App.source.canManageAccount
                text: qsTr("Manage account")
                iconName: "open"
                onClicked: App.source.manageAccount()
            }

            AppButton {
                visible: App.source.signedIn
                text: qsTr("Sign out")
                onClicked: signOutDialog.open()
            }

            AppButton {
                visible: !App.source.signedIn && !App.source.signingIn
                text: qsTr("Sign in")
                variant: AppButton.Accent
                onClicked: section.signInRequested()
            }
        }
    }

    SettingsDivider {}

    SettingsRow {
        title: qsTr("Status")
        description: App.source.accountStatus
        descriptionError: App.source.accountStatusError

        AppButton {
            visible: App.source.signingIn
            text: qsTr("Cancel")
            onClicked: App.source.cancelSignIn()
        }
    }

    ConfirmDialog {
        id: signOutDialog

        title: qsTr("Sign out?")
        description: qsTr("Your synced library will require signing in again on this device.")
        confirmLabel: qsTr("Sign out")
        onConfirmed: App.source.signOut()
    }
}
