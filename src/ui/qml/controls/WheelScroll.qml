import QtQuick
import LastMusicPlayer

// Mouse-wheel scrolling for a vertical Flickable, ListView or GridView. Qt's
// own wheel handling turns a notch into a short decelerating flick of about
// 72px and averages it with the notches before, which feels slow next to
// other apps. Here a notch moves 100px (scaled by the system's lines-per-notch
// setting, 3 by default) with a short ease, notches add up while the wheel
// keeps turning, and at either end the views around it scroll instead.
// Sideways wheel moves (tilt, Shift) are left to the view.
//
// Put it inside the view it scrolls: `WheelScroll { flickable: list }`.
WheelHandler {
    id: handler

    required property Flickable flickable

    // The user scrolled `flickable` with the wheel. The glide animates
    // contentY, which a Flickable does not count as the user moving it (no
    // movementStarted), so views that follow something listen here.
    signal scrolled()
    // Where the current run of notches is heading, in `glide.target`.
    property real destination: 0
    readonly property real notch: 100 * Math.max(1, Application.styleHints.wheelScrollLines) / 3

    // The nearest view around `item` that scrolls (a Flickable, ListView or
    // GridView), or null.
    function outerView(item) {
        for (let up = item.parent; up; up = up.parent) {
            if (up.contentY !== undefined && up.flickableDirection !== undefined)
                return up
        }
        return null
    }

    // Moves `view` by `pixels` (down when positive) within its limits.
    // False when it is already at that end and not on its way anywhere.
    function scroll(view, pixels) {
        const moving = glide.running && glide.target === view
        const top = view.originY - view.topMargin
        const bottom = Math.max(top, view.originY + view.contentHeight + view.bottomMargin - view.height)
        const from = moving ? handler.destination : view.contentY
        const to = Math.max(top, Math.min(bottom, from + pixels))
        if (to === from && !moving)
            return false
        if (glide.running && glide.target !== view) {
            // Another view's run ends where it was heading.
            glide.stop()
            glide.target.contentY = handler.destination
        }
        handler.destination = to
        glide.stop()
        glide.target = view
        glide.from = view.contentY
        glide.to = to
        glide.start()
        return true
    }

    target: null

    onWheel: event => {
        const delta = event.angleDelta.y
        if (delta === 0 || (event.modifiers & Qt.ShiftModifier)) {
            event.accepted = false
            return
        }
        // This view first; at its end, the views around it in turn. Qt's
        // handler does not hand an event it saw on to the next one.
        const pixels = -delta / 120 * handler.notch
        for (let view = handler.flickable; view; view = handler.outerView(view)) {
            if (view.interactive && handler.scroll(view, pixels)) {
                event.accepted = true
                if (view === handler.flickable)
                    handler.scrolled()
                return
            }
        }
        event.accepted = false
    }

    readonly property NumberAnimation glide: NumberAnimation {
        target: handler.flickable
        property: "contentY"
        duration: Motion.reduced ? 0 : 180
        easing.type: Easing.OutCubic
    }
}
