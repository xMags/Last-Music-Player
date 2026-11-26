import QtQuick

// Holds a list or grid a set distance below the top of its header until the
// user scrolls it. For a control in the header (a tab, a filter) that
// changes what the view shows: the view opens at the same distance, so the
// control stays under the pointer, and keeps it while the new content and
// header take their size. The owner ends the hold when the wheel, the scroll
// bar or the keys move the view (none of them starts a Flickable movement);
// a drag ends it here. While it holds, the view is put back whenever it
// moves on its own.
//
// Put it beside the view: `StartPlace { id: place; flickable: grid }`.
QtObject {
    id: place

    property Flickable flickable: null
    // The distance below the header's top.
    property real offset: 0
    property bool holding: false

    function start(offset: real) {
        place.offset = offset
        place.holding = true
        Qt.callLater(place.apply)
    }

    function release() {
        place.holding = false
    }

    // Scrolls to `offset`, or as far as the content reaches.
    function apply() {
        const view = place.flickable
        if (!place.holding || view === null)
            return
        const furthest = Math.max(0, view.contentHeight + view.topMargin + view.bottomMargin - view.height)
        view.contentY = view.originY - view.topMargin + Math.min(place.offset, furthest)
    }

    readonly property Connections follow: Connections {
        target: place.flickable
        enabled: place.holding

        // The content (header included) grew or shrank: the rows of the new
        // tab arrived, or the header took its size.
        function onContentHeightChanged() {
            Qt.callLater(place.apply)
        }

        // It moved on its own (a model swapped under it resets it to the
        // top, with no size change to notice): back to the place. The
        // user's own scrolling ends the hold before it moves.
        function onContentYChanged() {
            Qt.callLater(place.apply)
        }

        function onMovementStarted() {
            place.release()
        }
    }
}
