pragma ComponentBehavior: Bound

import QtQuick
import LastMusicPlayer

// One section of Home (a row of App.home.sections): its tier label when it
// starts a tier, then its body by kind. A shelf's cards are built when the
// section comes near the visible part of the page or when its turn in the
// page's staged build comes, whichever is first; until then the shelf shows
// skeleton cards at the same height, so nothing below it moves.
Column {
    id: section

    required property string key
    required property string title
    required property string tier
    required property string kind
    required property bool link
    // The page: menus open in its coordinates.
    required property Item page
    required property StagedBuild stage
    required property TrackMenus menus
    required property ActionMenu cardMenu
    required property ActionMenu itemMenu

    readonly property TrackListModel shelfModel: key === "listenAgain" ? App.home.listenAgain
        : key === "mostPlayed" ? App.home.mostPlayed
        : key === "liked" ? App.home.liked
        : App.home.recentlyAdded

    // This section's turn in the staged build (0 until it has one).
    property int ticket: 0
    property bool cardsBuilt: false

    // Built once, then kept. A section counts as near view only once the
    // page has placed it (y > 0: the search field and greeting come first),
    // since every new section starts at the top before layout.
    Binding on cardsBuilt {
        when: (section.y > 0 && sectionInView.inView)
            || (section.ticket > 0 && section.stage.served >= section.ticket)
        value: true
        restoreMode: Binding.RestoreNone
    }

    signal searchRequested()

    Component.onCompleted: section.ticket = section.stage.take()

    // Not drawn while it lies wholly outside the visible part of the page.
    opacity: sectionInView.inView ? 1 : 0

    InViewWatch {
        id: sectionInView

        target: section
    }

    // .home-tier's 18px under the label collapses with a catalogue shelf's
    // 28px top margin and a chart row's 30px.
    HomeTierLabel {
        text: section.tier
        below: section.kind === "catalog" ? 28 : section.kind === "charts" ? 30 : 18
    }

    Loader {
        width: parent.width
        sourceComponent: section.kind === "hero" ? heroSection
            : section.kind === "mixes" ? mixesSection
            : section.kind === "catalog" ? catalogSection
            : section.kind === "charts" ? chartsSection
            : section.kind === "status" ? statusSection
            : section.kind === "welcome" ? welcomeSection
            : songsSection
    }

    Component {
        id: heroSection

        HomeHero {
            width: parent ? parent.width : 0
            // .home-hero margin-bottom 44px: 24px here and the next tier
            // label's 20px.
            bottomPadding: 44 - 20
            onMenuRequested: (item, at) => section.menus.open(item, at.x, at.y, false)
            onTileMenuRequested: (row, item, at) => {
                App.home.prepareQuickTileMenu(row)
                const place = item.mapToItem(section.page, at.x, at.y)
                section.cardMenu.openAt(place.x, place.y)
            }
        }
    }

    Component {
        id: welcomeSection

        Column {
            HomeWelcomeCard {
                width: section.width
                onSearchRequested: section.searchRequested()
            }

            // .home-welcome: margin-bottom 8px, then the tier's gap.
            Item { width: 1; height: 8 + 32 }
        }
    }

    Component {
        id: statusSection

        HomeStatusSection {}
    }

    Component {
        id: mixesSection

        Column {
            Shelf {
                width: section.width
                title: section.title
                linkText: qsTr("See all ›")
                model: App.home.mixes
                cardsBuilt: section.cardsBuilt
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
                        const place = mix.mapToItem(section.page, at.x, at.y)
                        section.cardMenu.openAt(place.x, place.y)
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
                linkText: section.link ? qsTr("See all ›") : ""
                model: App.home.catalogShelf(section.key)
                cardsBuilt: section.cardsBuilt
                skeletonRound: App.home.catalogShelf(section.key)?.leadsWithRound ?? false
                // Every Home card below the hero is 184px (views.css 2779-2786).
                cardWidth: 184
                onLinkClicked: App.home.seeAll(section.key)

                delegate: Card {
                    id: catalogCard

                    required property int index
                    required title
                    required subtitle
                    required artworkUrl
                    required artworkId
                    required round

                    width: 184
                    hasMenu: true
                    onClicked: App.home.playCatalogCard(section.key, catalogCard.index)
                    // The card's menu (openCatalogItemMenu); a song's is the song menu.
                    onMenuRequested: (at, fromKeyboard) => {
                        if (!App.catalogItemMenu.prepare(App.home.catalogShelf(section.key), catalogCard.index))
                            return
                        const place = catalogCard.mapToItem(section.page, at.x, at.y)
                        section.itemMenu.openAt(place.x, place.y)
                        section.itemMenu.keyboardActive = fromKeyboard
                    }
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
                linkText: section.link ? qsTr("See all ›") : ""
                model: App.home.chartRow(section.key)
                cardsBuilt: section.cardsBuilt
                cardWidth: 184
                cardHeight: 184

                onLinkClicked: App.home.seeAll(section.key)

                delegate: ChartCard {
                    id: chartCard

                    required property int index
                    required artSource
                    required artworkUrl
                    required name
                    required family
                    required kindLabel
                    required kindColor
                    required cardSubtitle

                    onClicked: App.home.openChart(section.key, chartCard.index)
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
                linkText: qsTr("See all ›")
                model: section.shelfModel
                cardsBuilt: section.cardsBuilt
                cardWidth: 184
                // Listen Again's cards carry when each song was played.
                cardHeight: cardWidth + 12 + 22 + 18 + (section.key === "listenAgain" ? 2 + 16 : 0)
                onLinkClicked: App.home.seeAll(section.key)

                delegate: TrackTile {
                    id: card

                    required property int index
                    required property string playedWhen

                    dateText: section.key === "listenAgain" ? playedWhen : ""

                    width: 184
                    onClicked: App.home.playSong(section.key, card.index)
                    onMenuRequested: (at, fromKeyboard) => {
                        App.home.prepareSongMenu(section.key, card.index)
                        section.menus.open(card, at.x, at.y, fromKeyboard)
                    }
                }
            }

            Item { width: 1; height: 32 }
        }
    }
}
