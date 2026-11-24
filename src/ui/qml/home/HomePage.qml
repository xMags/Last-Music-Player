pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
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

                // .home-tier's 18px under the label collapses with a
                // catalogue shelf's 28px top margin and a chart row's 30px.
                TierLabel {
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
                    id: welcomeSection

                    Column {
                        WelcomeCard {
                            width: section.width
                        }

                        // .home-welcome: margin-bottom 8px, then the tier's gap.
                        Item { width: 1; height: 8 + 32 }
                    }
                }

                Component {
                    id: mixesSection

                    Column {
                        Shelf {
                            width: section.width
                            title: section.title
                            linkText: qsTr("See all ›")
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
                            linkText: section.link ? qsTr("See all ›") : ""
                            model: App.home.catalogShelf(section.key)
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
                                required artworkLabel
                                required round

                                width: 184
                                hasMenu: true
                                onClicked: App.home.playCatalogCard(section.key, catalogCard.index)
                                // The card's menu (openCatalogItemMenu); a song's is the song menu.
                                onMenuRequested: (at, fromKeyboard) => {
                                    if (!App.catalogItemMenu.prepare(App.home.catalogShelf(section.key), catalogCard.index))
                                        return
                                    const place = catalogCard.mapToItem(page, at.x, at.y)
                                    itemMenu.openAt(place.x, place.y)
                                    itemMenu.keyboardActive = fromKeyboard
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

                        // homeNotice: a quiet card with an alert, the message
                        // and a "Try again" pill (styles.css 2925-2953).
                        Rectangle {
                            visible: !App.home.catalogSkeleton && App.home.catalogError !== ""
                            width: Math.min(640, section.width)
                            implicitHeight: noticeRow.implicitHeight + 2 * 14
                            radius: 12
                            color: Theme.surface
                            border.width: 1
                            border.color: Theme.stroke

                            RowLayout {
                                id: noticeRow

                                x: 16
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width - 2 * 16
                                spacing: 12

                                Icon {
                                    name: "alert"
                                    size: 16
                                    color: Theme.fgTertiary
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: App.home.catalogError
                                    font: Theme.viewSubtitle
                                    color: Theme.fgSecondary
                                    wrapMode: Text.Wrap
                                }

                                NoticeButton {
                                    text: qsTr("Try again")
                                    onClicked: App.home.retryCatalog()
                                }
                            }
                        }

                        // .home-refresh-status: cached charts, 11px tertiary,
                        // with an inline Refresh.
                        Row {
                            visible: !App.home.catalogSkeleton && App.home.catalogError === "" && App.home.catalogCached
                            topPadding: 7
                            spacing: 0

                            Text {
                                text: qsTr("Showing cached charts. ")
                                font: Theme.refreshStatus
                                color: Theme.fgTertiary
                            }

                            LinkButton {
                                text: qsTr("Refresh")
                                labelFont: Theme.refreshStatus
                                labelColor: Theme.fg
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
                            linkText: qsTr("See all ›")
                            model: section.shelfModel
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

    // homeWelcomeMarkup: For you before the listener has played or liked
    // anything. The hero's field lines behind, so it reads as part of Home;
    // an eyebrow, the title, a sentence, and Browse charts and Search
    // (.home-welcome-*, styles.css 2866-2922).
    component WelcomeCard: Item {
        id: welcome

        implicitHeight: Math.max(216, copy.implicitHeight + 2 * 28)

        Rectangle {
            id: welcomeBox

            anchors.fill: parent
            radius: 16
            color: Theme.mixSrgb(Theme.surface, "transparent", 0.82)
            border.width: 1
            border.color: Theme.stroke
            clip: true

            HeroField {
                anchors.fill: parent
                anchors.margins: 1
                running: welcome.visible
            }

            // radial-gradient(120% 140% at 100% 50%, accent 16%, transparent 60%)
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                radius: 15
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.4; color: Qt.alpha(Theme.accentColor, 0) }
                    GradientStop { position: 1; color: Qt.alpha(Theme.accentColor, 0.16) }
                }
            }
        }

        Column {
            id: copy

            x: 32
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(520, parent.width - 2 * 32)
            spacing: 10

            Row {
                spacing: 6

                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    name: "sparkle"
                    size: 14
                    color: Theme.accentColor
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Getting started").toUpperCase()
                    font: Theme.welcomeEyebrow
                    color: Theme.accentColor
                }
            }

            Text {
                width: parent.width
                text: qsTr("Your music starts here")
                font: Theme.welcomeTitle
                lineHeight: Theme.lineHeightWelcomeTitle
                lineHeightMode: Text.FixedHeight
                color: Theme.fg
                wrapMode: Text.Wrap
            }

            Text {
                width: parent.width
                text: qsTr("Play or like a few songs. Last Music turns them into mixes made for you, and keeps your recent songs here.")
                font: Theme.welcomeText
                lineHeight: Theme.lineHeightWelcomeText
                lineHeightMode: Text.FixedHeight
                color: Theme.fgSecondary
                wrapMode: Text.Wrap
            }

            // .home-welcome-actions: 10px apart, 6px more above.
            Flow {
                width: parent.width
                topPadding: 6
                spacing: 10

                WelcomeButton {
                    primary: true
                    iconName: "flame"
                    text: qsTr("Browse charts")
                    onClicked: App.navigator.openView(Route.Browse)
                }

                WelcomeButton {
                    iconName: "browse"
                    text: qsTr("Search")
                    onClicked: page.openBrowse("")
                }
            }
        }
    }

    // .home-welcome-button: a 40px pill padded 0 18px, an icon 8px before
    // 14px 600 text; the accent fill for the first. Shrinks to 98% pressed.
    component WelcomeButton: T.Button {
        id: welcomeButton

        property bool primary: false
        property string iconName: ""

        implicitHeight: 40
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        leftPadding: 18
        rightPadding: 18
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        scale: down && !Motion.reduced ? 0.98 : 1

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }

        contentItem: Row {
            spacing: 8

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: welcomeButton.iconName
                size: 15
                color: welcomeButton.primary ? Theme.accentInk : Theme.fg
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: welcomeButton.text
                font: Theme.welcomeButton
                color: welcomeButton.primary ? Theme.accentInk : Theme.fg
            }
        }

        background: ButtonSurface {
            radius: height / 2
            border.width: welcomeButton.primary ? 0 : 1
            border.color: Theme.stroke
            // .primary:hover is brightness(1.06).
            color: welcomeButton.primary ? (welcomeButton.hovered ? Theme.brightness(Theme.accentColor, 1.06) : Theme.accentColor)
                                         : (welcomeButton.hovered ? Theme.hover : Theme.surface)
            focusShown: welcomeButton.visualFocus
        }
    }

    // .home-notice-button: a 32px outlined pill, padded 0 14px, 13px 600;
    // the hover colour on hover.
    component NoticeButton: T.Button {
        id: noticeButton

        implicitHeight: 32
        implicitWidth: implicitContentWidth + leftPadding + rightPadding
        leftPadding: 14
        rightPadding: 14
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        font: Theme.settingsButtonLabel

        HoverHandler {
            cursorShape: Qt.PointingHandCursor
        }

        contentItem: Text {
            text: noticeButton.text
            font: noticeButton.font
            color: Theme.fg
            verticalAlignment: Text.AlignVCenter
        }

        background: ButtonSurface {
            radius: height / 2
            border.width: 1
            border.color: Theme.stroke
            color: noticeButton.hovered ? Theme.hover : "transparent"
            focusShown: noticeButton.visualFocus
        }
    }

    // A tier's label: small capitals with a line to the right. .home-tier's
    // 52px top margin collapses with the 32px every section ends with, so
    // 20px more here; the hero ends with its own 44px (see heroSection).
    component TierLabel: Item {
        id: label

        property string text: ""
        property real below: 18

        width: parent ? parent.width : 0
        height: text === "" ? 0 : 20 + tierText.implicitHeight + below
        visible: text !== ""

        Text {
            id: tierText

            y: 20
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
            // .home-hero margin-bottom 44px: 24px here and the next tier
            // label's 20px.
            bottomPadding: 44 - 20
            onMenuRequested: (item, at) => menus.open(item, at.x, at.y, false)
            onTileMenuRequested: (row, item, at) => {
                App.home.prepareQuickTileMenu(row)
                const place = item.mapToItem(page, at.x, at.y)
                cardMenu.openAt(place.x, place.y)
            }
        }
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
