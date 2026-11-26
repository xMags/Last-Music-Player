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
        return kind === Route.Library || kind === Route.Offline || (kind === Route.Collection && !host.showsArtist())
            || host.showsCatalog()
    }

    // The catalogue's album, playlist, chart and shelf pages (App.catalogPage).
    function showsCatalog(): bool {
        const kind = App.navigator.currentKind
        if (kind === Route.CatalogDetail) {
            const type = App.navigator.current.param("type")
            return type === "album" || type === "playlist"
        }
        return kind === Route.SourceAlbum || kind === Route.ExternalPlaylist || kind === Route.Chart
            || kind === Route.Shelf || kind === Route.ArtistSection || kind === Route.Shared
    }

    // The catalogue's artist page (App.catalogArtist).
    function showsCatalogArtist(): bool {
        const kind = App.navigator.currentKind
        return kind === Route.SourceArtist
            || (kind === Route.CatalogDetail && App.navigator.current.param("type") === "artist")
    }

    // A Collection route of an artist shows the artist page.
    function showsArtist(): bool {
        return App.navigator.currentKind === Route.Collection && App.navigator.current.param("kind") === "artist"
    }

    function showCurrent() {
        const ownScroll = host.scrollsItself(App.navigator.currentKind)
        scroller.contentY = App.navigator.returning && !ownScroll ? App.navigator.returnScrollOffset : 0
        if (!App.navigator.returning && !Motion.reduced)
            enter.restart()
        if (App.navigator.returning && App.navigator.currentFocusName !== "")
            focusReturn.restart()
    }

    // Keeps which control has keyboard focus in the page's history entry.
    Connections {
        target: host.Window.window

        function onActiveFocusItemChanged() {
            const item = host.Window.activeFocusItem
            if (!item || !App.focusPlace.hasKeyboardFocus(item) || !content.item)
                return
            const index = App.focusPlace.indexOf(content, item)
            if (index >= 0)
                App.navigator.setCurrentFocus(App.focusPlace.nameOf(item), index)
        }
    }

    // Back: once the page and its scroll position are back, the control that
    // had keyboard focus gets it again (view-place.ts).
    Timer {
        id: focusReturn

        interval: 120
        onTriggered: {
            const target = App.focusPlace.find(content, App.navigator.currentFocusName,
                                               App.navigator.currentFocusIndex)
            if (target)
                target.forceActiveFocus(Qt.TabFocusReason)
        }
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
            laneTopMargin: Theme.radius2xl
        }

        WheelScroll { flickable: scroller }

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
                    : App.navigator.currentKind === Route.Browse ? browsePage
                    : App.navigator.currentKind === Route.Settings ? settingsPage
                    : App.navigator.currentKind === Route.Library ? libraryPage
                    : App.navigator.currentKind === Route.Offline ? offlinePage
                    : App.navigator.currentKind === Route.Charts ? chartsPage
                    : App.navigator.currentKind === Route.Collection ? (host.showsArtist() ? artistPage : collectionPage)
                    : host.showsCatalog() ? catalogPage
                    : host.showsCatalogArtist() ? catalogArtistPage
                    : placeholderPage
            }
        }
    }

    Component {
        id: settingsPage

        SettingsPage {}
    }

    Component {
        id: browsePage

        BrowsePage {}
    }

    Component {
        id: homePage

        HomePage {}
    }

    Component {
        id: chartsPage

        ChartsPage {}
    }

    Component {
        id: offlinePage

        OfflinePage {}
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
        id: catalogPage

        CatalogPage {}
    }

    Component {
        id: catalogArtistPage

        ArtistPage {
            model: App.catalogArtist
        }
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
