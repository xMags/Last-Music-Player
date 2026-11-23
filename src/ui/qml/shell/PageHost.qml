import QtQuick
import LastMusicPlayer

// Shows the page of the current route inside the content scroller. A page
// opened fresh starts at the top and rises 10px into place as it fades in
// (router.ts enterPage: 280 ms); a page returned to with Back keeps no
// animation and gets its scroll position back.
Item {
    id: host

    function showCurrent() {
        scroller.contentY = App.navigator.returning ? App.navigator.currentScrollOffset : 0
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
        contentWidth: width
        contentHeight: page.height
        boundsBehavior: Flickable.StopAtBounds

        // The history entry keeps how far its page is scrolled.
        onContentYChanged: App.navigator.setCurrentScrollOffset(contentY)

        AppScrollBar.vertical: AppScrollBar {}

        Item {
            id: page

            width: scroller.width
            height: Math.max(scroller.height, content.implicitHeight)
            transform: Translate { id: rise }

            // The page of the current route; routes whose page a later part
            // of the work builds show its title.
            Loader {
                id: content

                width: parent.width
                sourceComponent: App.navigator.currentKind === Route.Settings ? settingsPage : placeholderPage
            }
        }
    }

    Component {
        id: settingsPage

        SettingsPage {}
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
