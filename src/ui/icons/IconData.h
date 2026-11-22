#pragma once

#include <Qt>

#include <vector>

// Static icon tables. The .cpp files that define them are generated from the
// design's SVG icons; edit the generator output only to add or fix an icon.
namespace lmp::ui::icondata {

// How one part of an icon is painted.
enum class IconPaint {
    None,    // not painted
    Current, // the icon's colour (the text colour where it is shown)
    Fixed,   // the colour stored with the part
};

// One run of SVG elements that share their paint. Drawn in table order.
struct IconPartData {
    const char* path;        // SVG path data in a 24 x 24 view box
    IconPaint fill;
    const char* fillColor;   // "#RRGGBB" when fill is Fixed, otherwise null
    IconPaint stroke;
    const char* strokeColor; // "#RRGGBB" when stroke is Fixed, otherwise null
    double strokeWidth;
    bool evenOdd;            // fill-rule: evenodd
    Qt::PenCapStyle cap;
    Qt::PenJoinStyle join;
};

// An icon with no parts is built in code (see IconSet).
struct IconEntry {
    const char* name;
    std::vector<IconPartData> parts;
};

[[nodiscard]] const std::vector<IconEntry>& lineIcons();
[[nodiscard]] const std::vector<IconEntry>& solidIcons();
[[nodiscard]] const std::vector<IconEntry>& deviceLogos();

} // namespace lmp::ui::icondata
