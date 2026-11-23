import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The typed display name, for the Offline and API key sources: a field of
// up to 40 characters and Save, which lights up once the trimmed text differs
// from the saved name. Enter saves too. An empty name is allowed; the profile
// then shows the default name.
SettingsSection {
    id: section

    iconName: "person"
    title: qsTr("Profile")

    SettingsRow {
        title: qsTr("Display name")
        description: qsTr("Shown in the greeting and the sidebar")

        RowLayout {
            spacing: 8

            PromptTextField {
                id: field

                Layout.preferredWidth: 220
                maximumLength: 40
                placeholderText: qsTr("Listener")
                text: App.settings.displayName
                onAccepted: section.save()
            }

            AppButton {
                id: save

                text: qsTr("Save")
                variant: AppButton.Accent
                enabled: App.settings.isDisplayNameEdit(field.text)
                onClicked: section.save()
            }
        }
    }

    // Shows exactly what was saved, trimmed.
    function save() {
        if (!App.settings.isDisplayNameEdit(field.text))
            return
        field.text = App.settings.saveDisplayName(field.text)
    }
}
