import QtQuick
import QtQuick.Shapes
import LastMusicPlayer

// One part of an icon: path data with its own fill, stroke and line style.
ShapePath {
    id: shapePath

    required property iconPart part
    required property color tint

    fillColor: part.fillFor(tint)
    strokeColor: part.strokeFor(tint)
    strokeWidth: part.strokeWidth
    fillRule: part.fillRule
    capStyle: part.capStyle
    joinStyle: part.joinStyle

    PathSvg {
        path: shapePath.part.path
    }
}
