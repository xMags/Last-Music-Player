pragma ComponentBehavior: Bound

import QtQuick

// Whether `target`, grown by `margin` above and below, overlaps the visible
// part of the Flickable it scrolls in (the page's). A page gives a section
// opacity 0 while it is out of view: the renderer then skips the whole
// subtree, where a clip does not, while the section keeps its size, its
// place, its focus and its accessibility. Without an enclosing Flickable the
// target counts as in view.
QtObject {
    id: watch

    required property Item target
    property real margin: 0

    // The scrolling view, found once.
    property Flickable viewport: null

    // Read again as the page scrolls, resizes or its layout changes (the
    // target moving inside the page, or growing).
    readonly property bool inView: {
        if (watch.viewport === null)
            return true
        const layout = watch.viewport.height + watch.viewport.contentHeight + watch.target.y + watch.target.height
        const top = watch.target.mapToItem(watch.viewport.contentItem, 0, 0).y
        return layout >= 0 && top + watch.target.height + watch.margin > watch.viewport.contentY
            && top - watch.margin < watch.viewport.contentY + watch.viewport.height
    }

    Component.onCompleted: {
        for (let up = watch.target.parent; up; up = up.parent) {
            if (up instanceof Flickable) {
                watch.viewport = up as Flickable
                return
            }
        }
    }
}
