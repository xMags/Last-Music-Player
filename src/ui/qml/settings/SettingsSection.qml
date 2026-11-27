import QtQuick
import LastMusicPlayer

// A titled group of Settings (.settings-section, views.css 1116-1136): the
// heading 12px above one card that holds the rows.
Column {
    id: section

    property string iconName: ""
    property string title: ""
    property string note: ""
    // A line under the card saying how an action went (.settings-feedback):
    // the accent colour, or the danger colour when `feedbackError`.
    property string feedback: ""
    property bool feedbackError: false
    default property alias rows: card.rows

    spacing: 12

    // Left out when the group above already names the card.
    SettingsSectionHead {
        width: section.width
        visible: section.title !== ""
        iconName: section.iconName
        title: section.title
        note: section.note
    }

    SettingsCard {
        id: card

        width: section.width
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
