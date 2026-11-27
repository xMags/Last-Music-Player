import QtQuick
import LastMusicPlayer

// Stands in for a page that a later part of the work builds: its title, in
// the section-title style.
Item {
    id: page

    required property string title

    implicitHeight: heading.implicitHeight + 2 * 32

    AppText {
        id: heading

        x: 32
        y: 32
        text: page.title
        font: Theme.sectionTitle
        lineHeight: Theme.lineHeightSectionTitle
        lineHeightMode: Text.FixedHeight
        color: Theme.fg
    }
}
