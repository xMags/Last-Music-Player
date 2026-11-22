import QtQuick
import LastMusicPlayer

// Buttons that open the action menu, the text prompt and the sample toasts,
// and a line saying what was picked last.
GallerySection {
    id: root

    required property GalleryViewModel model

    title: qsTr("Menus, dialog and toasts")

    Row {
        spacing: 12

        AppButton {
            id: menuButton

            text: qsTr("Open menu")
            iconName: "more"
            onClicked: menu.openAt(menuButton.x, menuButton.y + menuButton.height + 6)
        }

        AppButton {
            text: qsTr("Open dialog")
            onClicked: prompt.open()
        }

        AppButton {
            text: qsTr("Show a toast")
            variant: AppButton.Accent
            onClicked: root.model.showSampleToast()
        }
    }

    Text {
        text: root.model.lastAction === "" ? qsTr("Nothing picked yet") : qsTr("Last: %1").arg(root.model.lastAction)
        font: Theme.body
        color: Theme.fgSecondary
    }

    ActionMenu {
        id: menu

        items: root.model.menuItems
        onTriggered: itemId => root.model.report(itemId)
    }

    TextPromptDialog {
        id: prompt

        title: qsTr("Create playlist")
        description: qsTr("Give the new playlist a name.")
        fieldLabel: qsTr("Name")
        placeholderText: qsTr("My playlist")
        submitLabel: qsTr("Create")
        onSubmitted: value => {
            if (value === "") {
                errorText = qsTr("Enter a name.")
                return
            }
            root.model.report(qsTr("Created %1").arg(value))
            finish()
        }
        onCancelled: root.model.report(qsTr("Dialog cancelled"))
    }
}
