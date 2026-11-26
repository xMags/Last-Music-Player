import QtQuick
import LastMusicPlayer

// The flight layer (flight.ts, cover-morph.ts): a copy of a card's cover
// drawn over the content panel at the card's size, moved to the header
// cover's place by position and scale only, while the real header cover
// hides (App.coverFlight.landing). Drawn at the card's size, the copy asks
// for the same picture the card already shows, so it comes from the image
// cache at once instead of loading again (a skeleton block meanwhile). The
// layer clips to the panel, so a flight never passes over the rest of the
// window. A new navigation, or another flight, lands the current one at once.
// Skipped with reduced motion.
Item {
    id: layer

    clip: true

    function land() {
        flight.stop()
        copy.visible = false
        App.coverFlight.landing = null
    }

    function begin(from, source, artworkId, round, target) {
        land()
        if (Motion.reduced || !target || !target.visible)
            return
        // The header has its final place after this turn's layout.
        Qt.callLater(() => {
            if (!target || target.width < 8)
                return
            const to = target.mapToItem(layer, 0, 0, target.width, target.height)
            const start = layer.mapFromItem(null, from.x, from.y, from.width, from.height)
            const visibleArea = Qt.rect(0, 0, layer.width, layer.height)
            const onScreen = r => r.x + r.width > 0 && r.y + r.height > 0 && r.x < visibleArea.width && r.y < visibleArea.height
            if (!onScreen(to) || !onScreen(start))
                return
            copy.source = source
            copy.artworkId = artworkId
            const landedScale = to.width / start.width
            // The header's rounding once scaled to its size.
            copy.radius = round ? start.width / 2 : 12 / landedScale
            copy.width = start.width
            copy.height = start.height
            xAnimation.from = start.x
            xAnimation.to = to.x
            yAnimation.from = start.y
            yAnimation.to = to.y
            scaleAnimation.to = landedScale
            copy.x = start.x
            copy.y = start.y
            copy.scale = 1
            App.coverFlight.landing = target
            copy.visible = true
            flight.restart()
        })
    }

    ArtTile {
        id: copy

        visible: false
        transformOrigin: Item.TopLeft
        big: true
    }

    ParallelAnimation {
        id: flight

        onFinished: layer.land()

        NumberAnimation { id: xAnimation; target: copy; property: "x"; duration: Motion.coverFlight; easing: Motion.flight }
        NumberAnimation { id: yAnimation; target: copy; property: "y"; duration: Motion.coverFlight; easing: Motion.flight }
        NumberAnimation { id: scaleAnimation; target: copy; property: "scale"; from: 1; duration: Motion.coverFlight; easing: Motion.flight }
    }

    Connections {
        target: App.coverFlight

        function onFlightWanted(from, source, artworkId, round, target) {
            layer.begin(from, source, artworkId, round, target)
        }
    }

    Connections {
        target: App.navigator

        function onCurrentChanged() {
            if (flight.running)
                layer.land()
        }
    }
}
