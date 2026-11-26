import QtQuick
import QtQuick.Effects
import LastMusicPlayer

// CSS backdrop-filter: blur() (RULES.md section 8): the part of `source`
// that lies behind this item, blurred, under the surface's own tinted fill.
// Only that region is captured (ShaderEffectSource.sourceRect), so the cost
// follows the surface's size, not the window's. `source` must not contain
// this item (that would capture itself); leave it null and the surface keeps
// its plain tinted fill, the reference's look without blur.
//
// Put it first inside the surface, filling it, and give the same corner
// radius as the surface.
Item {
    id: root

    // The content behind the surface.
    property Item source: null
    // The CSS blur length in pixels.
    property real blurPx: 12
    // CSS saturate(): 1 leaves colours as they are.
    property real saturate: 1
    property real cornerRadius: 0

    readonly property bool active: source !== null && visible && width > 0 && height > 0
    // The ancestors whose moves call place(), each connected once. A former
    // ancestor is let go when the chain changes; one destroyed since has
    // already dropped its connections and has no signals left to read.
    property var followed: []

    anchors.fill: parent

    // Where the surface lies over the source. A binding would not see its
    // ancestors move (a popup opening, a page scrolling), so every ancestor's
    // move asks again; nothing runs while nothing moves.
    function place() {
        capture.sourceRect = root.active ? root.mapToItem(root.source, 0, 0, root.width, root.height)
                                         : Qt.rect(0, 0, 0, 0)
    }

    function unfollow() {
        for (const item of root.followed) {
            if (!item || !item.xChanged)
                continue
            item.xChanged.disconnect(root.place)
            item.yChanged.disconnect(root.place)
        }
        root.followed = []
    }

    function follow() {
        root.unfollow()
        const chain = []
        for (let item = root; item; item = item.parent) {
            item.xChanged.connect(root.place)
            item.yChanged.connect(root.place)
            chain.push(item)
        }
        root.followed = chain
        root.place()
    }

    Component.onCompleted: follow()
    // The ancestors outlive this item (a card of a shelf goes while the page
    // stays); their moves would keep calling place() on what is gone.
    Component.onDestruction: unfollow()
    // A popup's background gets its parent after it is made.
    onParentChanged: follow()
    onActiveChanged: place()
    onWidthChanged: place()
    onHeightChanged: place()

    ShaderEffectSource {
        id: capture

        anchors.fill: parent
        visible: false
        live: true
        hideSource: false
        recursive: false
        sourceItem: root.active ? root.source : null
    }

    Rectangle {
        id: mask

        anchors.fill: parent
        radius: root.cornerRadius
        visible: false
        layer.enabled: true
    }

    MultiEffect {
        anchors.fill: parent
        visible: root.active
        source: capture
        blurEnabled: true
        // MultiEffect's blur reaches blurMax pixels at blur 1; a CSS blur
        // length is a standard deviation, which reaches about twice as far.
        blurMax: Math.min(64, Math.round(root.blurPx * 2))
        blur: 1
        // MultiEffect grows itself by the blur's reach on every side by
        // default, so a blur can spread past its item. Here that drew the
        // blurred, saturated backdrop around the surface, outside its outline
        // and its mask. The capture is exactly the surface, so it stays inside.
        autoPaddingEnabled: false
        saturation: root.saturate - 1
        maskEnabled: root.cornerRadius > 0
        maskSource: mask
    }
}
