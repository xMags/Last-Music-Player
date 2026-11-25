import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The API key source's connection (MainWindow.xaml SettingsApiKeyCard): the
// provider's address and a key, Connect, which tests them before anything is
// stored, and Disconnect, which removes both. The key field is never filled
// from the stored key, and is cleared once a key is connected. The result of
// the last action shows under the buttons.
SettingsSection {
    id: section

    iconName: "lock"
    title: qsTr("Provider connection")
    note: App.source.providerConnected ? qsTr("Connected") : qsTr("Not connected")

    // The address as stored. The field is an edit buffer, not bound to it:
    // the source's signal fires on any mode or connection change (the
    // network coming and going), which would put the stored address over
    // what is being typed.
    property string storedUrl: ""

    Component.onCompleted: {
        section.storedUrl = App.source.providerBaseUrl
        urlField.text = section.storedUrl
    }

    Connections {
        target: App.source

        function onConnected() {
            keyField.text = ""
        }

        function onChanged() {
            const stored = App.source.providerBaseUrl
            if (stored === section.storedUrl)
                return
            section.storedUrl = stored
            urlField.text = stored
        }
    }

    SettingsRow {
        title: qsTr("Provider URL")
        description: qsTr("The address of your provider")

        PromptTextField {
            id: urlField

            width: 280
            enabled: !App.source.connecting
            placeholderText: "http://127.0.0.1:4527"
            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
            onAccepted: keyField.forceActiveFocus()
        }
    }

    SettingsDivider {}

    SettingsRow {
        title: qsTr("API key")
        description: App.source.providerConnected ? qsTr("A key is stored. Enter a new one to replace it.") : qsTr("Kept in Windows Credential Manager")

        PromptTextField {
            id: keyField

            width: 280
            enabled: !App.source.connecting
            echoMode: TextInput.Password
            placeholderText: qsTr("Enter a new API key")
            onAccepted: App.source.connectProvider(urlField.text, keyField.text)
        }
    }

    SettingsDivider {}

    Item {
        width: parent ? parent.width : 0
        implicitHeight: actions.implicitHeight + 2 * 16

        ColumnLayout {
            id: actions

            x: 20
            y: 16
            width: parent.width - 2 * 20
            spacing: 10

            RowLayout {
                spacing: 10

                AppButton {
                    text: qsTr("Connect")
                    iconName: "link"
                    variant: AppButton.Accent
                    enabled: !App.source.connecting
                    onClicked: App.source.connectProvider(urlField.text, keyField.text)
                }

                AppButton {
                    text: qsTr("Disconnect")
                    enabled: App.source.providerConnected && !App.source.connecting
                    onClicked: App.source.disconnectProvider()
                }

                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 6
                    text: App.source.apiKeyStatus !== "" ? App.source.apiKeyStatus : qsTr("Ready")
                    font: Theme.settingsRowDescription
                    color: Theme.fgSecondary
                    elide: Text.ElideRight
                }
            }

            Text {
                Layout.fillWidth: true
                text: qsTr("Your provider decides how identifiers resolve, how auth works and where audio comes from. See docs/PROVIDER_API.md.")
                font: Theme.settingsRowDescription
                lineHeight: Theme.lineHeightSettingsRowDescription
                lineHeightMode: Text.FixedHeight
                color: Theme.fgTertiary
                wrapMode: Text.Wrap
            }
        }
    }
}
