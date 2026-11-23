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

    readonly property real gutter: 32

    implicitHeight: content.implicitHeight + 40

    Component.onCompleted: {
        App.home.refreshGreeting()
        App.home.refresh()
    }
    Component.onDestruction: App.home.leave()

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
                required property string kind
                required property bool link
                readonly property TrackListModel shelfModel: key === "listenAgain" ? App.home.listenAgain
                    : key === "mostPlayed" ? App.home.mostPlayed
                    : key === "liked" ? App.home.liked
                    : App.home.recentlyAdded

                width: content.width

                TierLabel { text: section.tier }

                Loader {
                    width: parent.width
                    sourceComponent: section.kind === "hero" ? heroSection
                        : section.kind === "mixes" ? mixesSection
                        : section.kind === "catalog" ? catalogSection
                        : section.kind === "charts" ? chartsSection
                        : section.kind === "status" ? statusSection
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
                    id: catalogSection

                    Column {
                        Shelf {
                            width: section.width
                            title: section.title
                            linkText: section.link ? qsTr("See all") : ""
                            model: App.home.catalogShelf(section.key)

                            delegate: Card {
                                id: catalogCard

                                required property int index
                                required title
                                required subtitle
                                required artworkUrl
                                required artworkId
                                required artworkLabel
                                required round

                                width: 156
                                hasMenu: false
                                onClicked: App.home.playCatalogCard(section.key, catalogCard.index)
                            }
                        }

                        Item { width: 1; height: 32 }
                    }
                }

                Component {
                    id: chartsSection

                    Column {
                        Shelf {
                            width: section.width
                            title: section.title
                            linkText: section.link ? qsTr("See all") : ""
                            model: App.home.chartRow(section.key)
                            cardWidth: 184
                            cardHeight: 184

                            delegate: ChartCard {
                                required artSource
                                required artworkUrl
                                required name
                                required family
                                required kindLabel
                                required kindColor
                                required cardSubtitle
                            }
                        }

                        Item { width: 1; height: 32 }
                    }
                }

                Component {
                    id: statusSection

                    Column {
                        width: section.width
                        spacing: 12
                        bottomPadding: 32

                        // Nothing to show yet: three shelves of placeholders.
                        Repeater {
                            model: App.home.catalogSkeleton ? 3 : 0

                            Column {
                                width: section.width
                                spacing: 14

                                Skeleton {
                                    width: 180
                                    height: 20
                                }

                                SkeletonGrid {
                                    width: section.width
                                    count: 6
                                }
                            }
                        }

                        Row {
                            visible: !App.home.catalogSkeleton
                            spacing: 12

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: App.home.catalogError !== "" ? App.home.catalogError
                                                                    : qsTr("Showing cached charts.")
                                font: Theme.stateText
                                color: Theme.fgSecondary
                            }

                            AppButton {
                                text: App.home.catalogError !== "" ? qsTr("Try again") : qsTr("Refresh")
                                onClicked: App.home.retryCatalog()
                            }
                        }
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
