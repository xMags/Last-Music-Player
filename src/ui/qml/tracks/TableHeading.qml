import QtQuick
import LastMusicPlayer

// A column heading of the track table (.track-table-head, views.css 161-172):
// 11px bold, 0.08em apart, tertiary.
AppText {
    font: Theme.tableHeading
    color: Theme.fgTertiary
    elide: Text.ElideRight
}
