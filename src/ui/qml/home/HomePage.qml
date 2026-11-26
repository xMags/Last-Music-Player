pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// Home (views/home.ts renderFeed): the greeting (the part of the day over the
// name), the hero, then each section App.home lists, a tier label leading
// the first section of its tier. In Account mode the tiers follow the
// service's layout and hold the catalogue's shelves and chart rows. An empty
// library shows an empty state with "Choose music folder". The page scrolls
// in the content panel.
Item {
    id: page

    readonly property real gutter: Theme.contentGutter
    // The right side also keeps the scrollbar lane (scrollbar-gutter: stable).
    readonly property real rightGutter: Theme.contentGutter + Theme.contentScrollLane

    implicitHeight: content.implicitHeight + 24

    // Browse with `typed` in its search field and the caret after it.
    function openBrowse(typed) {
        App.search.query = typed
        App.navigator.openView(Route.Browse)
        Qt.callLater(() => App.search.requestFocus(true))
    }

    Component.onCompleted: {
        App.home.refreshGreeting()
        App.home.refresh()
    }
    Component.onDestruction: App.home.leave()

    Column {
        id: content

        x: page.gutter
        width: page.width - (page.gutter + page.rightGutter)
        spacing: 0

        // The search box leads (views/home.ts render), 16px above and 28px
        // below. Searching happens on Browse: a click or typing goes there,
        // carrying the typed text; tabbing through the box does not.
        Item { width: 1; height: 16 }

        SearchField {
            id: homeSearch

            width: Math.min(implicitWidth, parent.width)
            anchors.horizontalCenter: parent.horizontalCenter
            placeholderText: qsTr("Search songs, artists, albums, links…")
            shortcutHint: qsTr("Ctrl + K")
            onQueryEdited: query => {
                if (query !== "")
                    page.openBrowse(query)
            }
            onSubmitted: query => page.openBrowse(query)

            TapHandler {
                onTapped: page.openBrowse(homeSearch.text)
            }
        }

        Item { width: 1; height: 28 }

        // The greeting: the salute with its time-of-day mark, then the name.
        Column {
            width: parent.width
            bottomPadding: 22

            Row {
                spacing: 7

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: App.home.greetingTime
                    size: 18
                    color: App.home.greetingTime === "moon" ? Theme.accentColor : "#D4A03C"
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: App.home.salutation
                    font: Theme.homeLabel
                    color: Theme.fgSecondary
                    Accessible.name: App.home.salutation + ", " + App.profile.name
                }
            }

            Text {
                width: parent.width
                topPadding: 2
                text: App.profile.name
                font: Theme.homeGreeting
                lineHeight: Theme.lineHeightHomeGreeting
                lineHeightMode: Text.FixedHeight
                color: Theme.fg
                elide: Text.ElideRight
            }
        }

        EmptyState {
            width: parent.width
            visible: App.home.empty
            iconName: "music-note"
            title: qsTr("Your library is empty")
            text: qsTr("Choose the folder your music is in, and Home fills with your songs and mixes.")
            actionText: qsTr("Choose music folder")
            onActionClicked: App.librarySettings.changeFolder()
        }

        // Each section: its tier label when it starts a tier, then its body.
        Repeater {
            model: App.home.sections

            HomeSection {
                width: content.width
                page: page
                stage: stage
                menus: menus
                cardMenu: cardMenu
                itemMenu: itemMenu
                onSearchRequested: page.openBrowse("")
            }
        }
    }

    // Builds the shelves' cards one section per turn, in page order.
    StagedBuild {
        id: stage
    }

    ActionMenu {
        id: itemMenu

        items: App.catalogItemMenu.menu
        onTriggered: itemId => App.catalogItemMenu.trigger(itemId)
    }

    ActionMenu {
        id: cardMenu

        items: App.home.cardMenu
        onTriggered: itemId => App.home.triggerCardMenu(itemId)
    }

    TrackMenus {
        id: menus
    }
}
