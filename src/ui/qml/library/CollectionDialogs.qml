import QtQuick
import LastMusicPlayer

// The name prompt and the delete confirmation of App.collections: New
// playlist, New album and Rename playlist ask for a name; Delete playlist and
// Delete album ask first. One of each lives in the shell; whoever asked
// (Library, the sidebar, a collection page) only calls App.collections.
Item {
    id: dialogs

    Connections {
        target: App.collections

        function onPromptRequested() {
            namePrompt.text = App.collections.promptValue
            namePrompt.errorText = ""
            namePrompt.open()
        }

        function onConfirmRequested() {
            deleteConfirm.open()
        }
    }

    TextPromptDialog {
        id: namePrompt

        title: App.collections.promptTitle
        fieldLabel: App.collections.promptLabel
        placeholderText: App.collections.promptPlaceholder
        submitLabel: App.collections.promptSubmit
        maximumLength: App.collections.promptMaximumLength
        onSubmitted: value => {
            if (App.collections.submitPrompt(value))
                namePrompt.finish()
            else
                namePrompt.errorText = qsTr("That did not work. Try again in a moment.")
        }
    }

    ConfirmDialog {
        id: deleteConfirm

        title: App.collections.confirmTitle
        description: App.collections.confirmText
        confirmLabel: App.collections.confirmLabel
        onConfirmed: App.collections.confirm()
    }
}
