import QtQuick
import LastMusicPlayer

// One card of Settings with its rows: its title as a 13px uppercase label
// inside the card (none when the group above already names it), an optional
// note under it, and an optional line under the card saying how an action
// went. `shown` is the card's own condition. While the page is searched the
// card shows when its title or its group's matches (all its rows then), or
// while any of its rows does.
Column {
    id: section

    // Kept for the cards that set it; the label has no icon.
    property string iconName: ""
    property string title: ""
    property string note: ""
    property bool danger: false
    property bool shown: true
    // A line under the card saying how an action went (.settings-feedback):
    // the accent colour, or the danger colour when `feedbackError`.
    property string feedback: ""
    property bool feedbackError: false
    default property alias rows: card.rows

    // The group this card is in, found once it is placed.
    property SettingsGroup hostGroup: null
    readonly property bool searchForced: SettingsStyle.needle === ""
        || (section.title !== "" && SettingsStyle.matches(section.title))
        || (section.hostGroup !== null && section.hostGroup.groupSearchForced)

    visible: shown && (SettingsStyle.needle === "" || card.hasRows)
    spacing: 10

    Component.onCompleted: {
        for (let up = section.parent; up; up = up.parent) {
            if (up instanceof SettingsGroup) {
                section.hostGroup = up as SettingsGroup
                return
            }
        }
    }

    SettingsCard {
        id: card

        width: section.width
        danger: section.danger

        // The label: 18px above, 20px at the sides, 6px below.
        Item {
            objectName: "settingsCardLabel"
            width: parent ? parent.width : 0
            visible: section.title !== ""
            implicitHeight: visible ? label.y + label.implicitHeight + (note.visible ? 2 + note.implicitHeight : 0) + 6 : 0

            Text {
                id: label

                x: 20
                y: 18
                width: parent.width - 2 * 20
                text: section.title
                font.family: SettingsStyle.family
                font.pixelSize: 13
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                color: section.danger ? SettingsStyle.dangerText : SettingsStyle.label
                elide: Text.ElideRight
                Accessible.role: Accessible.Heading
                Accessible.name: section.title
            }

            Text {
                id: note

                x: 20
                y: label.y + label.implicitHeight + 2
                width: parent.width - 2 * 20
                visible: section.note !== ""
                text: section.note
                font.family: SettingsStyle.family
                font.pixelSize: 12
                color: SettingsStyle.description
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
