import QtQuick
import LastMusicPlayer

// One card of Settings with its rows: its title as a small uppercase label
// inside the card at the top (none when the group above already names it),
// an optional note under the label, and an optional line under the card
// saying how an action went. A `danger` card has a red border and label.
Column {
    id: section

    // Kept for the cards that set it; the label has no icon.
    property string iconName: ""
    property string title: ""
    property string note: ""
    property bool danger: false
    // A line under the card saying how an action went (.settings-feedback):
    // the accent colour, or the danger colour when `feedbackError`.
    property string feedback: ""
    property bool feedbackError: false
    default property alias rows: card.rows

    spacing: 10

    SettingsCard {
        id: card

        width: section.width
        danger: section.danger

        // The label: 18px above, 20px at the sides, 6px below.
        Item {
            width: parent ? parent.width : 0
            visible: section.title !== ""
            implicitHeight: visible ? Math.round(label.y + label.implicitHeight + (note.visible ? 2 + note.implicitHeight : 0) + 6) : 0

            Text {
                id: label

                x: 20
                y: 18
                width: parent.width - 2 * 20
                text: section.title
                font.family: Theme.settingsRowTitle.family
                font.pixelSize: 12
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                color: section.danger ? Theme.danger : Theme.fgTertiary
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
                Accessible.name: section.title
            }

            Text {
                id: note

                x: 20
                y: Math.round(label.y + label.implicitHeight + 2)
                width: parent.width - 2 * 20
                visible: section.note !== ""
                text: section.note
                font: Theme.settingsRowDescription
                lineHeight: Theme.lineHeightSettingsRowDescription
                lineHeightMode: Text.FixedHeight
                color: Theme.fgTertiary
                wrapMode: Text.Wrap
            }
        }
    }

    // margin: 0 4px
    Text {
        x: 4
        width: section.width - 2 * 4
        visible: section.feedback !== ""
        text: section.feedback
        font: Theme.settingsFeedback
        lineHeight: Theme.lineHeightSettingsFeedback
        lineHeightMode: Text.FixedHeight
        color: section.feedbackError ? Theme.danger : Theme.accentColor
        wrapMode: Text.Wrap
        Accessible.role: Accessible.StaticText
    }
}
