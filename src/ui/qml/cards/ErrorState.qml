import QtQuick
import LastMusicPlayer

// A page or section that could not load: the empty state with the alert icon
// in the danger colour and, by default, a Try again button.
EmptyState {
    iconName: "alert"
    error: true
    actionText: qsTr("Try again")
}
