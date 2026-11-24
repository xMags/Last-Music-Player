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
        description: App.collections.promptDescription
        submitLabel: App.collections.promptSubmit
        busyLabel: App.collections.promptBusyLabel
        maximumLength: App.collections.promptMaximumLength
        onSubmitted: value => {
            if (App.collections.submitPrompt(value))
                namePrompt.finish()
            else if (App.collections.promptBusy)
                namePrompt.busy = true
            else
                namePrompt.errorText = App.collections.promptError !== "" ? App.collections.promptError
                                                                          : qsTr("That did not work. Try again in a moment.")
        }

        Connections {
            target: App.collections

            // An import came back: close on success, else say why.
            function onPromptSettled(error: string) {
                if (error === "") {
                    namePrompt.finish()
                    return
                }
                namePrompt.busy = false
                namePrompt.errorText = error
            }
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
