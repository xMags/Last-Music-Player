import QtQuick
import LastMusicPlayer

// Data and privacy in Account mode (views/settings.ts 203-226; views.css
// 1588-1592): clearing the synced listening history, then, on a faint danger
// band, resetting the app's data on this PC. Both ask first; while one runs
// both buttons wait and the running one says so; how it went shows under
// the card.
SettingsSection {
    id: section

    iconName: "lock"
    title: qsTr("Data and privacy")
    feedback: App.dataPrivacy.message
    feedbackError: App.dataPrivacy.messageError

    readonly property bool idle: App.dataPrivacy.working === ""

    SettingsRow {
        title: qsTr("Clear listening history")
        description: App.dataPrivacy.historySubtitle

        SettingsButton {
            enabled: App.dataPrivacy.hasHistory && section.idle
            working: App.dataPrivacy.working === "history"
            text: working ? qsTr("Working…") : qsTr("Clear history")
            onClicked: historyDialog.open()
        }
    }

    // .settings-row-danger: a 24% danger line on top, a 4% danger wash.
    Rectangle {
        width: parent ? parent.width : 0
        implicitHeight: resetRow.implicitHeight + 1
        color: Theme.mixSrgb(Theme.danger, "transparent", 0.04)
        // The card's lower corners, inside its border.
        bottomLeftRadius: Theme.radiusLg - 1
        bottomRightRadius: Theme.radiusLg - 1

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.mixSrgb(Theme.danger, "transparent", 0.24)
        }

        SettingsRow {
            id: resetRow

            y: 1
            width: parent.width
            title: qsTr("Reset app data")
            titleDanger: true
            description: qsTr("Sign out and erase settings, queue, artwork and audio caches on this computer")

            SettingsButton {
                danger: true
                enabled: section.idle
                working: App.dataPrivacy.working === "reset"
                text: working ? qsTr("Working…") : qsTr("Reset…")
                onClicked: resetDialog.open()
            }
        }
    }

    SettingsConfirmDialog {
        id: historyDialog

        title: qsTr("Clear listening history?")
        description: qsTr("This permanently removes your synced play history and resets Listen Again and Most Played. Liked songs and playlists stay intact.")
        confirmLabel: qsTr("Clear history")
        onConfirmed: App.dataPrivacy.clearHistory()
    }

    SettingsConfirmDialog {
        id: resetDialog

        title: qsTr("Reset %1?").arg(AppInfo.displayName)
        description: qsTr("This signs you out and erases %1 settings, queue, and cached audio on this device. Your synced likes, playlists, and history stay online.").arg(AppInfo.displayName)
        confirmLabel: qsTr("Reset app")
        onConfirmed: App.dataPrivacy.resetAppData()
    }
}
