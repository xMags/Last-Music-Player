pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Shapes
import LastMusicPlayer

// The marks of a genre tile in their 96-unit box (GenreArtworkTile.cpp
// BuildPrimitive): dots, rings and ovals (some dashed), bars, squares,
// outlined boxes and rectangles, columns on a common baseline, arches open
// at the bottom and the triangle, all in the tile's light ink.
Item {
    id: mark

    property var marks: []
    property color ink: "white"

    width: 96
    height: 96

    Repeater {
        model: mark.marks

        Item {
            id: part

            required property var modelData
            readonly property string kind: modelData.kind
            readonly property bool outlined: kind === "ring" || kind === "oval" || kind === "outlineBox"
                                             || kind === "outlineRectangle"

            x: modelData.x
            y: modelData.y
            width: modelData.width
            height: modelData.height
            rotation: modelData.rotation

            // Filled shapes and outlines without dashes.
            Rectangle {
                anchors.fill: parent
                visible: part.kind !== "triangle" && part.kind !== "arch" && !(part.outlined && part.modelData.dashed)
                radius: part.kind === "dot" || part.kind === "ring" || part.kind === "oval"
                        ? Math.min(width, height) / 2 : part.modelData.radius
                color: part.outlined ? "transparent" : mark.ink
                border.width: part.outlined ? part.modelData.stroke : 0
                border.color: mark.ink
            }

            // A dashed ring: 2 on, 2 off.
            Shape {
                anchors.fill: parent
                visible: part.outlined && part.modelData.dashed
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: mark.ink
                    strokeWidth: part.modelData.stroke
                    strokeStyle: ShapePath.DashLine
                    dashPattern: [2 / Math.max(1, part.modelData.stroke), 2 / Math.max(1, part.modelData.stroke)]
                    fillColor: "transparent"
                    PathAngleArc {
                        centerX: part.width / 2
                        centerY: part.height / 2
                        radiusX: (part.width - part.modelData.stroke) / 2
                        radiusY: (part.height - part.modelData.stroke) / 2
                        sweepAngle: 360
                    }
                }
            }

            // An arch: the outline without its bottom edge, rounded at the top.
            Item {
                anchors.fill: parent
                visible: part.kind === "arch"
                clip: true

                Rectangle {
                    width: parent.width
                    height: parent.height + part.modelData.radius + part.modelData.stroke
                    radius: part.modelData.radius
                    color: "transparent"
                    border.width: part.modelData.stroke
                    border.color: mark.ink
                }
            }
        }
    }

    // The triangle is drawn in the box's own units.
    Shape {
        anchors.fill: parent
        visible: mark.marks.some(item => item.kind === "triangle")
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: "transparent"
            fillColor: mark.ink
            startX: 48; startY: 14
            PathLine { x: 90; y: 84 }
            PathLine { x: 6; y: 84 }
            PathLine { x: 48; y: 14 }
        }
    }
}
