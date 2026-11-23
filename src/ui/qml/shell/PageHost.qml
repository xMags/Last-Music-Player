import QtQuick
import LastMusicPlayer

// Shows the page of the current route inside the content scroller. A page
// opened fresh starts at the top and rises 10px into place as it fades in
// (router.ts enterPage: 280 ms); a page returned to with Back keeps no
// animation and gets its scroll position back.
Item {
    id: host

    // Pages that scroll themselves (long lists) fill the panel; they keep
    // and restore their own position (ScrollMemory).
    function scrollsItself(kind: int): bool {
        return kind === Route.Library || (kind === Route.Collection && !host.showsArtist())
    }

    // A Collection route of an artist shows the artist page.
    function showsArtist(): bool {
        return App.navigator.currentKind === Route.Collection && App.navigator.current.param("kind") === "artist"
    }

    function showCurrent() {
        const ownScroll = host.scrollsItself(App.navigator.currentKind)
        scroller.contentY = App.navigator.returning && !ownScroll ? App.navigator.currentScrollOffset : 0
        if (!App.navigator.returning && !Motion.reduced)
            enter.restart()
    }

    clip: true

    Connections {
        target: App.navigator

        function onCurrentChanged() {
            host.showCurrent()
        }
    }

    Flickable {
        id: scroller

        anchors.fill: parent
        readonly property bool pageScrollsItself: host.scrollsItself(App.navigator.currentKind)

        contentWidth: width
        contentHeight: page.height
        interactive: !pageScrollsItself
        boundsBehavior: Flickable.StopAtBounds

        // The history entry keeps how far its page is scrolled.
        onContentYChanged: {
            if (!pageScrollsItself)
                App.navigator.setCurrentScrollOffset(contentY)
        }

        AppScrollBar.vertical: AppScrollBar {
            visible: !scroller.pageScrollsItself && size < 1
        }

        Item {
            id: page

            width: scroller.width
            height: scroller.pageScrollsItself ? scroller.height : Math.max(scroller.height, content.implicitHeight)
            transform: Translate { id: rise }

            // The page of the current route; routes whose page a later part
            // of the work builds show its title.
            Loader {
                id: content

                width: parent.width
                height: scroller.pageScrollsItself ? parent.height : implicitHeight
                sourceComponent: App.navigator.currentKind === Route.Home ? homePage
                    : App.navigator.currentKind === Route.Settings ? settingsPage
                    : App.navigator.currentKind === Route.Library ? libraryPage
                    : App.navigator.currentKind === Route.Collection ? (host.showsArtist() ? artistPage : collectionPage)
                    : placeholderPage
            }
        }
    }

    Component {
        id: settingsPage

        SettingsPage {}
    }

    Component {
        id: homePage

        HomePage {}
    }

    Component {
        id: libraryPage

        LibraryPage {}
    }

    Component {
        id: collectionPage

        CollectionPage {}
    }

    Component {
        id: artistPage

        ArtistPage {}
    }

    Component {
        id: placeholderPage

        PlaceholderPage {
            title: App.navigator.currentTitle
        }
    }

    ParallelAnimation {
        id: enter

        NumberAnimation { target: page; property: "opacity"; from: 0; to: 1; duration: Motion.page; easing: Motion.water }
        NumberAnimation { target: rise; property: "y"; from: 10; to: 0; duration: Motion.page; easing: Motion.water }
    }
}
