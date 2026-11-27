import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The typed display name, for the Offline and API key sources: a field of
// up to 40 characters and Save, which lights up once the trimmed text differs
// from the saved name. Enter saves too. An empty name is allowed; the profile
// then shows the default name.
SettingsSection {
    id: section

    title: qsTr("Profile")

    // The name as last saved. The field is an edit buffer, not bound to the
    // setting: that signal fires for every setting written (a switch, the
    // volume), which would put the saved name over what is being typed.
    property string savedName: ""

    Component.onCompleted: {
        section.savedName = App.settings.displayName
        field.text = section.savedName
    }

    Connections {
        target: App.settings

        function onPreferencesChanged() {
            const saved = App.settings.displayName
            if (saved === section.savedName)
                return
            section.savedName = saved
            field.text = saved
        }
    }

    SettingsRow {
        title: qsTr("Display name")
        description: qsTr("Shown in the greeting and the sidebar")

        RowLayout {
            spacing: 8

            SettingsTextField {
                id: field

                Layout.preferredWidth: 240
                maximumLength: 40
                placeholderText: qsTr("Listener")
                Accessible.name: qsTr("Display name")
                onAccepted: section.save()
            }

            SettingsButton {
                text: qsTr("Save")
                enabled: App.settings.isDisplayNameEdit(field.text)
                onClicked: section.save()
            }
        }
    }

    // Shows exactly what was saved, trimmed.
    function save() {
        if (!App.settings.isDisplayNameEdit(field.text))
            return
        const saved = App.settings.saveDisplayName(field.text)
        section.savedName = saved
        field.text = saved
    }
}
