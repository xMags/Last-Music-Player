import QtQuick
import LastMusicPlayer

// The one-time question, after signing in, whether to import this PC's
// listening history of remote songs into the account (App.historyImport).
// Closing it any other way than "Import" declines, as in the earlier
// versions.
ConfirmDialog {
    id: dialog

    title: qsTr("Import listening history")
    description: qsTr("Import compatible listening history from this PC? Only remote listening entries that the account service can resolve will be uploaded. Local files and paths stay private, and historical desktop play counts remain on this PC.")
    confirmLabel: qsTr("Import compatible history")
    cancelLabel: qsTr("Don't import")

    onConfirmed: App.historyImport.accept()
    onClosed: Qt.callLater(() => {
        if (App.historyImport.offering)
            App.historyImport.decline()
    })

    Connections {
        target: App.historyImport

        function onOfferingChanged() {
            if (App.historyImport.offering)
                dialog.open()
        }
    }
}
