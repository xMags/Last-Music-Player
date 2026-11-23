pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// The header every collection page shares (collection-header.ts; views.css
// 361-571): the back link, then a 16px-rounded card tinted by the cover's
// own colours (the cover drawn tiny and stretched, at 0.55, under a wash of
// the page colour from 20% to 68% across), the cover (188 to 232px, 28% of
// the card), and the words: eyebrow, title, description, the info line with
// its bold byline, and the action row resting on the cover's bottom edge:
// Play, Shuffle and the menu, and the List / Grid switch at the end.
Column {
    id: header

    property string backLabel: ""
    property string eyebrow: ""
    property string title: ""
    property string description: ""
    property string byline: ""
    property string info: ""
    property bool canPlay: true
    property bool showMenu: true
    // The menu has something besides playing (Rename, Delete), so it shows
    // while the collection is empty too.
    property bool menuWithoutSongs: false
    property bool showViewSwitch: true
    // The cover, as CollectionCover takes it.
    property string coverStyle: "image"
    property url artworkUrl
    property string artworkId: ""
    property string artworkLabel: ""
    property var collage: []
    property color gradientStart
    property color gradientEnd
    property string iconName: ""
    property string genre: ""
    property bool roundCover: false
    // A Follow button (artist pages), its state, and whether a change is on its way.
    // The round Share button, after Shuffle; Download after it.
    property bool showShare: false
    property bool showDownload: false
    property bool showFollow: false
    property bool followed: false
    property bool followBusy: false

    signal backClicked()
    signal playClicked()
    signal shuffleClicked()
    signal followClicked()
    signal shareClicked()
    signal downloadClicked()
    // `at` is in the header's coordinates.
    signal menuClicked(point at)

    spacing: 0

    // .collection-back: 16px above, 14px below, accent, a turned chevron.
    Item { width: 1; height: 16 }

    Item {
        implicitWidth: backRow.implicitWidth
        implicitHeight: backRow.implicitHeight

        Row {
            id: backRow

            spacing: 6

            Icon {
                anchors.verticalCenter: parent.verticalCenter
                name: "chevron"
                size: 14
                rotation: 180
                color: Theme.accentColor
            }

            Text {
                text: header.backLabel
                font: Theme.collectionBack
                color: Theme.accentColor
            }
        }

        HoverHandler { cursorShape: Qt.PointingHandCursor }
        TapHandler { onTapped: header.backClicked() }
        Accessible.role: Accessible.Link
        Accessible.name: header.backLabel
    }

    Item { width: 1; height: 14 }

    Item {
        id: card

        readonly property real artSize: Math.max(188, Math.min(232, width * 0.28))
        readonly property real gap: Math.max(20, Math.min(32, width * 0.034))

        width: parent.width
        height: Math.max(artSize, copy.implicitHeight) + 2 * 24

        // The tint: the cover's colours only, stretched over the card. It
        // fades in when it appears or changes (wash-in, views.css 402-403).
        // Cut to the card's corners inside its 1px border.
        RoundedClip {
            id: tint

            anchors.fill: parent
            anchors.margins: 1
            radius: 16 - 1
            opacity: 0.55

            function washIn() {
                if (!Motion.reduced)
                    washAnimation.restart()
            }

            Component.onCompleted: washIn()

            NumberAnimation {
                id: washAnimation

                target: tint
                property: "opacity"
                from: 0
                to: 0.55
                duration: 400
                easing: Motion.ease
            }

            Connections {
                target: header

                function onArtworkUrlChanged() {
                    tint.washIn()
                }
            }

            Image {
                anchors.fill: parent
                visible: header.coverStyle === "image" || header.coverStyle === "collage"
                source: header.coverStyle === "collage" && header.collage.length > 0 ? header.collage[0]
                    : header.artworkUrl
                sourceSize.width: 16
                sourceSize.height: 16
                fillMode: Image.Stretch
                smooth: true
                asynchronous: true
            }

            CollectionCover {
                anchors.fill: parent
                visible: header.coverStyle === "system" || header.coverStyle === "genre"
                         || (header.coverStyle === "image" && header.artworkUrl.toString() === "")
                coverStyle: header.coverStyle === "collage" ? "image" : header.coverStyle
                artworkId: header.artworkId
                gradientStart: header.gradientStart
                gradientEnd: header.gradientEnd
                genre: header.title
                radius: 0
                plain: true
            }
        }

        // The page colour over the tint, lighter on the cover's side.
        Rectangle {
            anchors.fill: parent
            radius: 16
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.alpha(Theme.bg, 0.20) }
                GradientStop { position: 1; color: Qt.alpha(Theme.bg, 0.68) }
            }
            border.width: 1
            border.color: Theme.stroke
        }

        Item {
            id: art

            x: 24
            y: 24
            width: card.artSize
            height: card.artSize

            BoxShadow {
                anchors.fill: parent
                layers: Theme.shadowArt
                radius: header.roundCover ? width / 2 : 12
                opacity: App.coverFlight.landing === headerCover ? 0 : 1
            }

            CollectionCover {
                id: headerCover

                anchors.fill: parent
                // Hidden while a card's cover flies into its place.
                opacity: App.coverFlight.landing === headerCover ? 0 : 1
                Component.onCompleted: App.coverFlight.headerShown(headerCover)
                coverStyle: header.coverStyle
                source: header.artworkUrl
                artworkId: header.artworkId
                label: header.artworkLabel
                collage: header.collage
                gradientStart: header.gradientStart
                gradientEnd: header.gradientEnd
                iconName: header.iconName
                genre: header.title
                radius: header.roundCover ? width / 2 : 12
            }
        }

        ColumnLayout {
            id: copy

            x: art.x + art.width + card.gap
            y: 24
            width: card.width - x - 24
            height: card.artSize
            spacing: 6

            Text {
                Layout.fillWidth: true
                text: header.eyebrow
                font: Theme.collectionEyebrow
                color: Theme.fgSecondary
            }

            Text {
                Layout.fillWidth: true
                text: header.title
                // clamp(26px, 5.4cqi, 44px) of the card.
                font.family: Theme.collectionTitle.family
                font.weight: Theme.collectionTitle.weight
                font.letterSpacing: Theme.collectionTitle.letterSpacing
                font.pixelSize: Math.max(26, Math.min(44, card.width * 0.054))
                color: Theme.fg
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            Text {
                Layout.fillWidth: true
                Layout.maximumWidth: 680
                visible: header.description !== ""
                text: header.description
                font: Theme.collectionDescription
                lineHeight: Theme.lineHeightCollectionDescription
                lineHeightMode: Text.FixedHeight
                color: Theme.fgSecondary
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }

            Row {
                Layout.fillWidth: true
                spacing: 0
                clip: true
                visible: header.info !== "" || header.byline !== ""

                Text {
                    visible: header.byline !== ""
                    text: header.byline
                    font: Theme.collectionInfoStrong
                    color: Theme.fg
                }

                Text {
                    text: (header.byline !== "" && header.info !== "" ? " · " : "") + header.info
                    font: Theme.collectionInfo
                    color: Theme.fgSecondary
                }
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                PlayPill {
                    visible: header.canPlay
                    onClicked: header.playClicked()
                }

                RoundAction {
                    visible: header.canPlay
                    iconName: "shuffle"
                    Accessible.name: qsTr("Shuffle")
                    onClicked: header.shuffleClicked()
                }

                RoundAction {
                    visible: header.showShare
                    iconName: "share"
                    Accessible.name: qsTr("Share")
                    onClicked: header.shareClicked()
                }

                RoundAction {
                    visible: header.showDownload
                    iconName: "download"
                    Accessible.name: qsTr("Download")
                    onClicked: header.downloadClicked()
                }

                RoundAction {
                    id: menuButton

                    visible: header.showMenu && (header.canPlay || header.menuWithoutSongs)
                    iconName: "more"
                    Accessible.name: qsTr("More options")
                    onClicked: header.menuClicked(menuButton.mapToItem(header, menuButton.width / 2, menuButton.height))
                }

                AppButton {
                    visible: header.showFollow
                    enabled: !header.followBusy
                    variant: header.followed ? AppButton.Subtle : AppButton.Standard
                    text: header.followed ? qsTr("Following") : qsTr("Follow")
                    iconName: header.followed ? "check" : "plus"
                    iconSize: 14
                    onClicked: header.followClicked()
                }

                Item { Layout.fillWidth: true }

                ViewSwitch {
                    visible: header.showViewSwitch && header.canPlay
                }
            }
        }
    }

    Item { width: 1; height: 18 }

    // Arrives in the page cascade when the page has just opened.
    CascadeIn {
        target: header
    }
}
