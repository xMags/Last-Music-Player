import QtQuick

// The text item of the whole app. Small text is drawn by Windows' own
// rasteriser (ClearType, main.cpp), the most legible at small sizes. Windows
// smooths those glyphs only across, not down, so the curves of larger and
// heavier text show steps and coloured edges; such text is drawn from its
// outlines on the GPU instead, smooth in both directions. Outlines are too
// soft for the smallest text, so it keeps ClearType.
Text {
    id: appText

    // The size asked for, in pixels; a size in points is 4/3 px (Qt uses 96
    // logical dots per inch).
    readonly property real requestedPixelSize: appText.font.pixelSize > 0 ? appText.font.pixelSize : appText.font.pointSize * 4 / 3
    // Set by text over the see-through window frame (the sidebar while a
    // backdrop shows): ClearType blends its coloured edges with the colour
    // under it, which there is the system backdrop it cannot see.
    property bool onGlass: false
    readonly property bool drawsOutlines: appText.onGlass
        || appText.requestedPixelSize >= 15
        || (appText.requestedPixelSize >= 13 && appText.font.weight >= Font.DemiBold)

    renderType: appText.drawsOutlines ? Text.CurveRendering : Text.NativeRendering
}
