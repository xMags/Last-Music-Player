import QtQuick
import QtQuick.Shapes
import LastMusicPlayer

// Draws one icon from the Icons table, scaled from its 24 x 24 view box to
// `size`. Parts with their own colour (device logos) keep it; the rest use
// `color`.
Item {
    id: root

    required property string name
    property real size: 16
    property color color: Theme.fg

    implicitWidth: size
    implicitHeight: size

    Shape {
        width: Icons.viewBoxSize
        height: Icons.viewBoxSize
        scale: root.size / Icons.viewBoxSize
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer

        // Icons.maxParts is 5: one path per part.
        IconShapePath { part: Icons.part(root.name, 0); tint: root.color }
        IconShapePath { part: Icons.part(root.name, 1); tint: root.color }
        IconShapePath { part: Icons.part(root.name, 2); tint: root.color }
        IconShapePath { part: Icons.part(root.name, 3); tint: root.color }
        IconShapePath { part: Icons.part(root.name, 4); tint: root.color }
    }
}
