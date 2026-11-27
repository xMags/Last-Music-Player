import QtQuick
import QtQuick.Templates as T
import LastMusicPlayer

// A chart card (catalog-chart-card.ts; views.css .home-chart-card): the drawn
// cover in the chart's palette, the catalogue's own picture over it when the
// chart has one, and the label band (hidden under a picture): a small line
// ("Top songs" with rising bars on drawn covers, a "City chart" pill on city
// cards), the name, and on city cards the chart's subtitle.
T.AbstractButton {
    id: card

    property url artSource
    property url artworkUrl
    property string name: ""
    // "city", "genre" or "global".
    property string family: "genre"
    property string kindLabel: ""
    property color kindColor: "white"
    property string cardSubtitle: ""

    readonly property bool drawn: family !== "city"

    implicitWidth: 184
    implicitHeight: 184
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: card.name

    HoverHandler {
        cursorShape: Qt.PointingHandCursor
    }

    contentItem: Item {
        ArtTile {
            id: drawnCover

            anchors.fill: parent
            source: card.artSource
            artworkId: "chart:" + card.name
            radius: Theme.radius2xl
            scale: card.hovered && !picture.loaded ? 1.045 : 1

            Behavior on scale {
                NumberAnimation { duration: 280; easing.type: Easing.OutCubic }
            }
        }

        ArtTile {
            id: picture

            anchors.fill: parent
            visible: card.artworkUrl.toString() !== ""
            source: card.artworkUrl
            radius: Theme.radius2xl
            scale: card.hovered ? 1.045 : 1

            Behavior on scale {
                NumberAnimation { duration: 280; easing.type: Easing.OutCubic }
            }
        }

        FocusRing {
            anchors.fill: parent
            shown: card.visualFocus
            controlRadius: Theme.radius2xl
        }

        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            anchors.bottomMargin: 13
            spacing: card.drawn ? 5 : 3
            visible: !picture.loaded

            // Drawn covers: rising bars and the label in the accent's light.
            Row {
                visible: card.drawn
                spacing: 6

                Item {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 11
                    height: 9

                    Repeater {
                        // Each bar's height and colour; the colour travels in
                        // the model so the delegate needs nothing outside it.
                        model: [4, 6, 9].map(height => ({ height: height, color: card.kindColor }))

                        Rectangle {
                            id: bar

                            required property int index
                            required property var modelData

                            x: bar.index * 4
                            y: 9 - bar.modelData.height
                            width: 3
                            height: bar.modelData.height
                            color: bar.modelData.color
                        }
                    }
                }

                AppText {
                    anchors.verticalCenter: parent.verticalCenter
                    text: card.kindLabel.toUpperCase()
                    font.pixelSize: 9
                    font.weight: Font.Bold
                    font.letterSpacing: 9 * 0.16
                    color: card.kindColor
                }
            }

            // City cards: a small pill.
            Rectangle {
                visible: !card.drawn
                width: pillText.implicitWidth + 14
                height: pillText.implicitHeight + 6
                radius: height / 2
                color: Qt.rgba(5 / 255, 13 / 255, 30 / 255, 0.26)
                border.width: 1
                border.color: Qt.rgba(1, 1, 1, 0.2)

                // .home-chart-kind: backdrop-filter: blur(5px) (views.css 857-859).
                BackdropBlur {
                    z: -1
                    source: picture.visible ? picture : drawnCover
                    blurPx: 5
                    cornerRadius: parent.radius
                }

                AppText {
                    id: pillText

                    anchors.centerIn: parent
                    text: card.kindLabel.toUpperCase()
                    font.pixelSize: 8
                    font.weight: Font.ExtraBold
                    font.letterSpacing: 8 * 0.12
                    color: Qt.rgba(1, 1, 1, 0.9)
                }
            }

            AppText {
                width: parent.width
                text: card.name
                font.family: card.drawn ? Theme.fontDisplay : font.family
                font.pixelSize: card.drawn ? 19 : 17
                font.weight: card.drawn ? Font.Bold : Font.DemiBold
                font.letterSpacing: card.drawn ? -19 * 0.02 : -17 * 0.01
                lineHeight: card.drawn ? 1.08 : 1.15
                maximumLineCount: card.drawn ? 2 : 1
                wrapMode: card.drawn ? Text.Wrap : Text.NoWrap
                elide: Text.ElideRight
                color: "white"
            }

            AppText {
                width: parent.width
                visible: card.cardSubtitle !== ""
                text: card.cardSubtitle
                font.pixelSize: 11
                elide: Text.ElideRight
                color: Qt.rgba(1, 1, 1, 0.72)
            }
        }
    }


    // Arrives in the page cascade when the page has just opened.
    CascadeIn {
        target: card.contentItem
    }
}
