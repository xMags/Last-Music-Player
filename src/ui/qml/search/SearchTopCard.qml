import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import LastMusicPlayer

// The top result (.search-top-card, views.css 1947-2030): the cover large
// on the left (round for an artist), as tall as the card and at most 46% of
// its width; beside it the title, a pill naming what it is and the detail,
// and at the foot a play glyph for a song or an arrow for a page. The card
// is tinted by its cover. A song plays; an artist or album opens.
T.AbstractButton {
    id: card

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: App.search.topKind === "song" ? qsTr("Play %1").arg(App.search.topTitle)
                                                   : qsTr("Open %1").arg(App.search.topTitle)

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    background: Rectangle {
        radius: 12
        color: card.hovered ? Theme.rowHover : Theme.surfaceAlt
        border.width: 1
        border.color: Theme.stroke

        // The cover's colours, faint, cut to the corners inside the border.
        RoundedClip {
            anchors.fill: parent
            anchors.margins: 1
            radius: 12 - 1
            opacity: 0.32

            Image {
                anchors.fill: parent
                source: App.search.topArtworkUrl
                sourceSize.width: 12
                sourceSize.height: 12
                fillMode: Image.Stretch
                smooth: true
                asynchronous: true
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: 12
            gradient: Gradient {
                orientation: Gradient.Vertical
                GradientStop { position: 0.3; color: "transparent" }
                GradientStop { position: 1; color: Qt.alpha(Theme.surfaceAlt, 0.7) }
            }
        }

        FocusRing {
            shown: card.visualFocus
        }
    }

    contentItem: Item {
        RowLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 20

            Item {
                Layout.fillHeight: true
                Layout.preferredWidth: Math.min(height, card.width * 0.46)

                ArtTile {
                    anchors.centerIn: parent
                    width: parent.width
                    height: width
                    radius: App.search.topKind === "artist" ? width / 2 : 10
                    big: true
                    shadow: true
                    source: App.search.topArtworkUrl
                    artworkId: App.search.topArtworkId
                    label: App.search.topArtworkLabel
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumWidth: 0
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    text: App.search.topTitle
                    font: Theme.searchTopTitle
                    lineHeight: Theme.lineHeightSearchTopTitle
                    lineHeightMode: Text.FixedHeight
                    color: Theme.fg
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10

                    Rectangle {
                        implicitWidth: kind.implicitWidth + 20
                        implicitHeight: kind.implicitHeight + 6
                        radius: height / 2
                        color: Qt.rgba(0, 0, 0, 0.32)

                        Text {
                            id: kind

                            anchors.centerIn: parent
                            text: App.search.topKind === "song" ? qsTr("Song")
                                : App.search.topKind === "artist" ? qsTr("Artist") : qsTr("Album")
                            font: Theme.searchTopKind
                            color: Theme.fg
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        visible: App.search.topKind !== "artist"
                        text: App.search.topKind === "song" ? App.search.topDetail : ""
                        font: Theme.trackArtist
                        color: Theme.fgSecondary
                        elide: Text.ElideRight
                    }
                }

                Item { Layout.fillHeight: true }

                Rectangle {
                    implicitWidth: 44
                    implicitHeight: 44
                    radius: 22
                    color: Theme.accentFill

                    Icon {
                        anchors.centerIn: parent
                        name: App.search.topKind === "song" ? "play" : "chevron"
                        size: App.search.topKind === "song" ? 18 : 16
                        color: "white"
                    }
                }
            }
        }
    }
}
