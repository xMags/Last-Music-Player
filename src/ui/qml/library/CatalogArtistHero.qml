import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// A catalogue artist's head (catalog.ts renderDetail; .catalog-hero,
// views.css 590-650): Back, then the round photo, between 180 and 260px
// (28% of the page), beside the words: ARTIST, the name (22 to 32px, one
// line), the subtitle (14px 600), the details (12px tertiary: year, genre,
// label, rating), the notes clipped to three lines with Read more / Show
// less under them, and Play, Shuffle, Follow and Share.
Column {
    id: hero

    required property var model

    signal backClicked()

    readonly property real artSize: Math.max(180, Math.min(260, width * 0.28))
    readonly property real gap: Math.max(20, Math.min(48, width * 0.04))

    Item { width: 1; height: 16 }

    BackLink {
        onClicked: hero.backClicked()
    }

    Item { width: 1; height: 22 }

    RowLayout {
        width: parent.width
        spacing: hero.gap

        Item {
            Layout.preferredWidth: hero.artSize
            Layout.preferredHeight: hero.artSize
            Layout.alignment: Qt.AlignVCenter

            // 0 12px 32px rgba(0, 0, 0, .16)
            BoxShadow {
                anchors.fill: parent
                radius: width / 2
                layers: Theme.shadowCatalogHeroArt
            }

            ArtTile {
                anchors.fill: parent
                radius: width / 2
                big: true
                source: hero.model.artworkUrl
                artworkId: hero.model.artworkId
                label: hero.model.artworkLabel
            }
        }

        // .catalog-hero-copy: padding-block 8px.
        Column {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignVCenter
            topPadding: 8
            bottomPadding: 8

            Text {
                text: qsTr("ARTIST")
                font: Theme.eyebrow
                color: Theme.fgSecondary
            }

            Text {
                width: parent.width
                topPadding: 8
                text: hero.model.name
                font.family: Theme.catalogHeroTitle.family
                font.weight: Theme.catalogHeroTitle.weight
                font.letterSpacing: Theme.catalogHeroTitle.letterSpacing * Math.max(22, Math.min(32, hero.width * 0.038)) / 32
                font.pixelSize: Math.max(22, Math.min(32, hero.width * 0.038))
                color: Theme.fg
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            Text {
                width: parent.width
                visible: hero.model.subtitle !== ""
                topPadding: 8
                text: hero.model.subtitle
                font: Theme.catalogHeroSubtitle
                color: Theme.fgSecondary
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                visible: hero.model.info !== ""
                topPadding: 9
                text: hero.model.info
                font: Theme.catalogHeroMeta
                color: Theme.fgTertiary
                elide: Text.ElideRight
            }

            // .catalog-description: 14px above, at most 760px, 13.5px at
            // 1.55; three lines until Read more.
            Text {
                id: notes

                property bool expanded: false

                width: Math.min(760, parent.width)
                visible: hero.model.description !== ""
                topPadding: 14
                text: hero.model.description
                font: Theme.collectionDescription
                lineHeight: 13.5 * 1.55
                lineHeightMode: Text.FixedHeight
                color: Theme.fgSecondary
                wrapMode: Text.Wrap
                maximumLineCount: expanded ? 10000 : 3
                elide: Text.ElideRight

                Connections {
                    target: hero.model

                    function onChanged() {
                        if (hero.model.description === "")
                            notes.expanded = false
                    }
                }
            }

            // .catalog-desc-toggle: 6px under, 13px in the accent; only
            // while the clip hides text.
            Item {
                width: 1
                height: 6
                visible: toggle.visible
            }

            LinkButton {
                id: toggle

                visible: notes.visible && (notes.truncated || notes.expanded)
                text: notes.expanded ? qsTr("Show less") : qsTr("Read more")
                labelFont: Theme.catalogDescToggle
                onClicked: notes.expanded = !notes.expanded
            }

            // .catalog-actions: 20px above, 10px apart.
            Item { width: 1; height: 20 }

            Row {
                spacing: 10

                PlayPill {
                    visible: hero.model.topSongs.count > 0
                    onClicked: hero.model.play()
                }

                RoundAction {
                    visible: hero.model.topSongs.count > 0
                    iconName: "shuffle"
                    toolTip: qsTr("Shuffle")
                    Accessible.name: qsTr("Shuffle")
                    onClicked: hero.model.shuffle()
                }

                FollowButton {
                    visible: hero.model.canFollow
                    followed: hero.model.followed
                    busy: hero.model.followBusy
                    onClicked: hero.model.toggleFollow()
                }

                // Share is always there: an artist's link needs no songs.
                RoundAction {
                    visible: App.share.available && hero.model.canShare
                    iconName: "share"
                    toolTip: qsTr("Share")
                    Accessible.name: qsTr("Share")
                    onClicked: hero.model.share()
                }
            }
        }
    }

    Item { width: 1; height: 18 }
}
