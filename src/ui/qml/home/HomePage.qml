pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// Home (views/home.ts renderFeed, built-in layout, offline sections): the
// greeting (the part of the day over the name), the hero, then each section
// App.home lists, a tier label leading the first section of its tier. An
// empty library shows an empty state with "Choose music folder". The page
// scrolls in the content panel.
Item {
    id: page

    readonly property real gutter: 32

    implicitHeight: content.implicitHeight + 40

    Component.onCompleted: {
        App.home.refreshGreeting()
        App.home.refresh()
    }

    Column {
        id: content

        x: page.gutter
        y: 24
        width: page.width - 2 * page.gutter
        spacing: 0

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

            Column {
                id: section

                required property string key
                required property string title
                required property string tier
                readonly property TrackListModel shelfModel: key === "listenAgain" ? App.home.listenAgain
                    : key === "mostPlayed" ? App.home.mostPlayed
                    : key === "liked" ? App.home.liked
                    : App.home.recentlyAdded

                width: content.width

                TierLabel { text: section.tier }

                Loader {
                    width: parent.width
                    sourceComponent: section.key === "hero" ? heroSection
                        : section.key === "mixes" ? mixesSection
                        : songsSection
                }

                Component {
                    id: mixesSection

                    Column {
                        Shelf {
                            width: section.width
                            title: section.title
                            linkText: qsTr("See all")
                            model: App.home.mixes
                            cardWidth: 184
                            cardHeight: 184
                            onLinkClicked: App.home.seeAll("mixes")

                            delegate: MixCard {
                                id: mix

                                required property int index

                                onClicked: {
                                    if (!mix.empty)
                                        App.home.playMix(mix.index)
                                }
                                onMenuRequested: at => {
                                    App.home.prepareMixMenu(mix.index)
                                    const place = mix.mapToItem(page, at.x, at.y)
                                    cardMenu.openAt(place.x, place.y)
                                }
                            }
                        }

                        Item { width: 1; height: 32 }
                    }
                }

                Component {
                    id: songsSection

                    Column {
                        Shelf {
                            width: section.width
                            title: section.title
                            linkText: qsTr("See all")
                            model: section.shelfModel
                            onLinkClicked: App.home.seeAll(section.key)

                            delegate: TrackTile {
                                id: card

                                required property int index

                                width: 156
                                onClicked: App.home.playSong(section.key, card.index)
                                onMenuRequested: (at, fromKeyboard) => {
                                    App.home.prepareSongMenu(section.key, card.index)
                                    menus.open(card, at.x, at.y, fromKeyboard)
                                }
                            }
                        }

                        Item { width: 1; height: 32 }
                    }
                }
            }
        }
    }

    // A tier's label: small capitals with a line to the right; 52px above.
    component TierLabel: Item {
        id: label

        property string text: ""

        width: parent ? parent.width : 0
        height: text === "" ? 0 : 52 + 16 + 18
        visible: text !== ""

        Text {
            id: tierText

            y: 52
            text: label.text
            font: Theme.homeLabel
            color: Theme.fgSecondary
        }

        Rectangle {
            x: tierText.width + 14
            anchors.verticalCenter: tierText.verticalCenter
            width: label.width - x
            height: 1
            color: Theme.stroke
        }
    }

    Component {
        id: heroSection

        HomeHero {
            width: parent ? parent.width : 0
            bottomPadding: 44 - 12
            onMenuRequested: (item, at) => menus.open(item, at.x, at.y, false)
            onTileMenuRequested: (row, item, at) => {
                App.home.prepareQuickTileMenu(row)
                const place = item.mapToItem(page, at.x, at.y)
                cardMenu.openAt(place.x, place.y)
            }
        }
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
