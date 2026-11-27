import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The API key source's connection: the provider's address and a key, then
// the state (a green dot once connected, or how the last try went) with
// Disconnect and Connect, which tests them before anything is stored. The
// key field is never filled from the stored key, and is cleared once a key
// is connected.
SettingsSection {
    id: section

    title: qsTr("Provider connection")

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

        SettingsTextField {
            id: urlField

            width: 340
            enabled: !App.source.connecting
            placeholderText: "https://"
            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
            Accessible.name: qsTr("Provider URL")
            onAccepted: keyField.forceActiveFocus()
        }
    }

    SettingsRow {
        title: qsTr("API key")
        description: App.source.providerConnected ? qsTr("A key is stored. Enter a new one to replace it.")
                                                  : qsTr("Kept in Windows Credential Manager")

        SettingsTextField {
            id: keyField

            width: 340
            enabled: !App.source.connecting
            echoMode: TextInput.Password
            placeholderText: qsTr("Enter a new API key")
            Accessible.name: qsTr("API key")
            onAccepted: App.source.connectProvider(urlField.text, keyField.text)
        }
    }

    SettingsBlock {
        keywords: qsTr("Connect") + "\n" + qsTr("Disconnect")

        RowLayout {
            width: parent.width
            height: Math.max(44, implicitHeight)
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                radius: 4
                color: App.source.providerConnected ? Theme.sourceReady : SettingsStyle.off
            }

            AppText {
                Layout.fillWidth: true
                text: App.source.apiKeyStatus !== "" ? App.source.apiKeyStatus
                    : App.source.providerConnected ? qsTr("Connected") : qsTr("Not connected")
                font.family: SettingsStyle.family
                font.pixelSize: 12
                color: SettingsStyle.description
                elide: Text.ElideRight
            }

            SettingsButton {
                visible: App.source.providerConnected
                enabled: !App.source.connecting
                text: qsTr("Disconnect")
                onClicked: App.source.disconnectProvider()
            }

            SettingsButton {
                primary: true
                enabled: !App.source.connecting
                working: App.source.connecting
                text: qsTr("Connect")
                onClicked: App.source.connectProvider(urlField.text, keyField.text)
            }
        }
    }
}
