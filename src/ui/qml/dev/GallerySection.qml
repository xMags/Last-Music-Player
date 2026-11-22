import QtQuick
import LastMusicPlayer

// One titled block of the design gallery.
Column {
    id: section

    required property string title
    default property alias content: body.data

    spacing: 16

    Text {
        text: section.title
        font: Theme.sectionTitle
        lineHeight: Theme.lineHeightSectionTitle
        lineHeightMode: Text.FixedHeight
        color: Theme.fg
    }

    Column {
        id: body

        width: section.width
        spacing: 16
    }
}
