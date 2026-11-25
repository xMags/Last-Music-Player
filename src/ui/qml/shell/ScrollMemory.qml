import QtQuick
import LastMusicPlayer

// Keeps how far a page's own list is scrolled in its history entry and,
// when the page is returned to with Back, scrolls the list there again.
// For pages that scroll themselves (PageHost.scrollsItself), whose list the
// content scroller cannot see. The offset is measured from the top of the
// list's header, so a header of any height restores to the same place.
// A list that loads in pages restores as far as its loaded rows reach.
// The page stays the same object when the route that follows uses the same
// page (one playlist, then another), so it restores on every route change,
// not only when it is made.
Item {
    id: memory

    // The list or grid; it may change (a new tab), and only the first one a
    // returned-to route shows is scrolled back.
    property Flickable target: null
    property bool restored: false

    function restore() {
        if (memory.restored || memory.target === null)
            return
        memory.restored = true
        if (!App.navigator.returning)
            return
        const offset = App.navigator.returnScrollOffset
        // Once the list has its rows (and the view its final target).
        Qt.callLater(() => {
            const view = memory.target
            if (view === null)
                return
            const furthest = Math.max(0, view.contentHeight + view.topMargin + view.bottomMargin - view.height)
            view.contentY = view.originY - view.topMargin + Math.min(offset, furthest)
        })
    }

    onTargetChanged: memory.restore()
    Component.onCompleted: memory.restore()

    Connections {
        target: App.navigator

        function onCurrentChanged() {
            memory.restored = false
            memory.restore()
        }
    }

    Connections {
        target: memory.target

        function onContentYChanged() {
            const view = memory.target
            App.navigator.setCurrentScrollOffset(view.contentY - view.originY + view.topMargin)
        }
    }
}
