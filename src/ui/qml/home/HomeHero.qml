pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import LastMusicPlayer

// Home's hero (home.ts homeHeroMarkup; styles.css 2342-2600): the "Jump back
// in" card for the last song played, tinted by its cover, with the field
// lines, the 184px cover, the eyebrow, title (two lines), artist, Play and
// the menu; then the shortcuts to the user's own collections, three across.
Column {
    id: hero

    // Where a song's or a tile's menu opens (the page's coordinates).
    signal menuRequested(Item item, point at)
    signal tileMenuRequested(int row, Item item, point at)

    spacing: 12

    Item {
        id: card

        width: parent.width
        height: Math.max(232, content.implicitHeight + 2 * 24)
        visible: App.home.hasHero

        // A different hero: the backdrop, cover and words fade in, 450 ms.
        readonly property string heroKey: App.home.heroTitle + "\n" + App.home.heroArtworkUrl
        property string shownKey: ""

        onHeroKeyChanged: {
            const first = card.shownKey === ""
            card.shownKey = card.heroKey
            if (!first && !Motion.reduced && card.visible)
                swap.restart()
        }

        ParallelAnimation {
            id: swap

            NumberAnimation { target: heroBackdrop; property: "opacity"; from: 0; to: 0.55; duration: 450; easing: Motion.ease }
            NumberAnimation { target: content; property: "opacity"; from: 0; to: 1; duration: 450; easing: Motion.ease }
        }

        // The backdrop and the field lines, cut to the card's rounded corners
        // inside its 1px border.
        RoundedClip {
            anchors.fill: parent
            anchors.margins: 1
            radius: 16 - 1

            // The cover's colours, stretched, quiet under the field lines.
            Item {
                id: heroBackdrop

                anchors.fill: parent
                opacity: 0.55

                Image {
                    anchors.fill: parent
                    source: App.home.heroArtworkUrl
                    sourceSize.width: 16
                    sourceSize.height: 16
                    fillMode: Image.Stretch
                    smooth: true
                    asynchronous: true
                    visible: status === Image.Ready
                }

                ArtTile {
                    anchors.fill: parent
                    visible: App.home.heroArtworkUrl.toString() === ""
                    radius: 0
                    artworkId: App.home.heroArtworkId
                    label: ""
                }
            }

            HeroField {
                anchors.fill: parent
                running: card.visible
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: 16
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.alpha(Theme.bg, 0.25) }
                GradientStop { position: 1; color: Qt.alpha(Theme.bg, 0.62) }
            }
            border.width: 1
            border.color: Theme.stroke
        }

        RowLayout {
            id: content

            x: 24
            width: parent.width - 48
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 24
            spacing: 24

            Item {
                Layout.preferredWidth: 184
                Layout.preferredHeight: 184
                Layout.alignment: Qt.AlignBottom

                ArtTile {
                    anchors.fill: parent
                    radius: 10
                    big: true
                    shadow: true
                    shadowLayers: Theme.shadowHeroArt
                    source: App.home.heroArtworkUrl
                    artworkId: App.home.heroArtworkId
                    label: App.home.heroArtworkLabel
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignBottom
                Layout.bottomMargin: 4
                spacing: 0

                Text {
                    text: qsTr("JUMP BACK IN")
                    font: Theme.collectionEyebrow
                    color: Theme.fgSecondary
                }

                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    text: App.home.heroTitle
                    font: Theme.heroTitle
                    lineHeight: Theme.lineHeightHeroTitle
                    lineHeightMode: Text.FixedHeight
                    color: Theme.fg
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    text: App.home.heroArtist
                    font: Theme.heroSub
                    color: Theme.fgSecondary
                    elide: Text.ElideRight
                }

                Row {
                    Layout.topMargin: 20
                    spacing: 10

                    HeroButton {
                        kind: HeroButton.Play
                        iconName: "play"
                        text: qsTr("Play")
                        Accessible.name: qsTr("Play %1 by %2").arg(App.home.heroTitle).arg(App.home.heroArtist)
                        onClicked: App.home.playHero()
                    }

                    HeroButton {
                        kind: HeroButton.Radio
                        iconName: "radio"
                        text: qsTr("Radio")
                        toolTip: qsTr("Start a radio from this song")
                        Accessible.name: qsTr("Start a radio from %1").arg(App.home.heroTitle)
                        onClicked: App.home.startHeroRadio()
                    }

                    HeroButton {
                        id: more

                        kind: HeroButton.More
                        iconName: "more"
                        toolTip: qsTr("More")
                        Accessible.name: qsTr("More options for %1").arg(App.home.heroTitle)
                        onClicked: {
                            App.home.prepareHeroMenu()
                            hero.menuRequested(more, Qt.point(0, more.height + 4))
                        }
                    }
                }
            }
        }
    }

    // The shortcuts: 64px rows, three across, 12px apart.
    Grid {
        width: parent.width
        visible: App.home.quickTiles.count > 0
        columns: 3
        spacing: 12

        Repeater {
            model: App.home.quickTiles

            QuickTile {
                id: tile

                required property int index

                width: (hero.width - 2 * 12) / 3
                onClicked: App.home.playQuickTile(tile.index)
                onMenuRequested: at => hero.tileMenuRequested(tile.index, tile, at)
            }
        }
    }
}
