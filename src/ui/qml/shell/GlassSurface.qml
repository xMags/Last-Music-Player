import QtQuick
import LastMusicPlayer

// A part of the window frame around the content panel (the sidebar, the
// rail, the player bar): see-through while the system backdrop shows, the
// surface colour otherwise or while `solid`. Text inside it draws from its
// outlines while it is see-through (AppText).
Rectangle {
    // Opaque even with a backdrop: the rail while it lies over the content.
    property bool solid: false
    readonly property bool seeThrough: App.windowChrome.backdropActive && !solid

    color: seeThrough ? "transparent" : Theme.surface
}
