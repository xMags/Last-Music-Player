import QtQuick
import LastMusicPlayer

// One item's part of the page cascade (page-cascade.ts): when it is created
// just after a page opened (App.cascade), `target` starts hidden 8px low and
// rises in, 320 ms, waiting 60 ms plus 28 ms per item before it. Put it
// inside the item and point `target` at the part that should move (a
// control's contentItem, so the control's own opacity binding is left alone).
// Nothing happens with reduced motion.
Item {
    id: cascade

    property Item target: null

    visible: false

    Component.onCompleted: {
        if (!cascade.target || Motion.reduced)
            return
        const index = App.cascade.nextIndex()
        if (index < 0)
            return
        cascade.target.transform = shift
        cascade.target.opacity = 0
        shift.y = 8
        wait.duration = 60 + index * 28
        rise.start()
    }

    Translate {
        id: shift
    }

    SequentialAnimation {
        id: rise

        PauseAnimation { id: wait }
        ParallelAnimation {
            NumberAnimation { target: cascade.target; property: "opacity"; to: 1; duration: 320; easing: Motion.water }
            NumberAnimation { target: shift; property: "y"; to: 0; duration: 320; easing: Motion.water }
        }
    }
}
