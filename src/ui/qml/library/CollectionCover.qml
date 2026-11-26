pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Layouts
import LastMusicPlayer

// A collection's cover (playlist-art.ts): a system playlist's gradient with
// its icon, a genre's drawn tile, a 2x2 collage of song covers, or one
// cover with the lettered placeholder behind it. `plain` draws only the
// colours (the header's tint): no icon and no letters.
Item {
    id: cover

    // "image", "collage", "system" or "genre".
    property string coverStyle: "image"
    property url source
    property string artworkId: ""
    property string label: ""
    property var collage: []
    property color gradientStart
    property color gradientEnd
    property string iconName: ""
    property string genre: ""
    property real radius: 12
    property bool plain: false
    // The owner is still loading; an empty `source` is not known yet.
    property bool pending: false

    DiagonalGradient {
        anchors.fill: parent
        visible: cover.coverStyle === "system"
        radius: cover.radius
        startColor: cover.gradientStart
        endColor: cover.gradientEnd

        Icon {
            anchors.centerIn: parent
            visible: !cover.plain
            name: cover.iconName
            size: Math.round(parent.width * 0.3)
            color: "white"
        }
    }

    Loader {
        anchors.fill: parent
        active: cover.coverStyle === "genre"
        sourceComponent: GenreTile {
            genre: cover.genre
            radius: cover.radius
        }
    }

    Item {
        anchors.fill: parent
        visible: cover.coverStyle === "collage"
        layer.enabled: visible
        layer.effect: MultiEffect {
            maskEnabled: true
            maskSource: collageMask
            maskThresholdMin: 0.5
            maskSpreadAtMin: 1.0
        }

        GridLayout {
            anchors.fill: parent
            columns: 2
            rowSpacing: 0
            columnSpacing: 0

            Repeater {
                model: cover.coverStyle === "collage" ? cover.collage : []

                ArtTile {
                    required property url modelData

                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 0
                    source: modelData
                    artworkId: cover.artworkId
                    label: ""
                }
            }
        }
    }

    ArtTile {
        anchors.fill: parent
        visible: cover.coverStyle === "image"
        radius: cover.radius
        big: true
        source: cover.source
        pending: cover.pending
        artworkId: cover.artworkId
        label: cover.plain ? "" : cover.label
    }

    Rectangle {
        id: collageMask

        anchors.fill: parent
        radius: cover.radius
        visible: false
        layer.enabled: true
    }
}
