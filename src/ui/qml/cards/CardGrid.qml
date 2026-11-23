import QtQuick
import LastMusicPlayer

// A grid of cards (.lib-grid, views.css 255-260): as many columns as fit at
// 160px or more, sharing the width (repeat(auto-fill, minmax(160px, 1fr))),
// 20px between columns and 24px between rows. It scrolls itself, so only
// the visible cards exist; `header` sits above the first row.
//
// Delegates read `cellSize` for their width.
GridView {
    id: grid

    // The item the scroll bar is drawn in, at its right edge (the page, so
    // the bar sits in the page's padding rather than over the cards).
    property Item scrollBarHost: null
    readonly property int columns: Math.max(1, Math.floor((width + columnGap) / (minimumCardWidth + columnGap)))
    readonly property real cardWidth: (width - (columns - 1) * columnGap) / columns
    property int minimumCardWidth: 160
    property int columnGap: 20
    property int rowGap: 24
    // The text under each cover (two lines).
    property int textHeight: 22 + 18

    cellWidth: cardWidth + columnGap
    cellHeight: cardWidth + 12 + textHeight + rowGap
    // The last column has no gap after it.
    rightMargin: -columnGap
    boundsBehavior: Flickable.StopAtBounds
    clip: true
    reuseItems: true

    AppScrollBar.vertical: AppScrollBar {
        parent: grid.scrollBarHost !== null ? grid.scrollBarHost : grid
        x: parent.width - width
        y: grid.scrollBarHost !== null ? grid.mapToItem(grid.scrollBarHost, 0, 0).y : 0
        height: grid.height
    }
}
