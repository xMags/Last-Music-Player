import QtQuick

// The text item of the whole app. Small text is drawn by Windows' own
// rasteriser (ClearType, main.cpp), sharpest at body sizes. Windows smooths
// those glyphs only across, not down, so the curves of large text (titles,
// the greeting's name) show steps; from 20px up the text is drawn from its
// outlines on the GPU instead, smooth in both directions and still sharp at
// that size.
Text {
    id: appText

    // The size asked for, in pixels; a size in points is 4/3 px (Qt uses 96
    // logical dots per inch).
    readonly property real requestedPixelSize: appText.font.pixelSize > 0 ? appText.font.pixelSize : appText.font.pointSize * 4 / 3

    renderType: appText.requestedPixelSize >= 20 ? Text.CurveRendering : Text.NativeRendering
}
