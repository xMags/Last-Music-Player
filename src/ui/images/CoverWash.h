#pragma once

#include <QImage>
#include <QSize>

namespace lmp::ui {

// A cover's colour wash (the web's cover-wash.ts): the cover drawn into a
// tiny picture, softened and more saturated, which is then stretched over a
// card. A tiny picture stretched that far is already a soft wash, so nothing
// is blurred while it is drawn.
//
// The cover is stretched to `size` plus 4 pixels each way, blurred (Gaussian,
// `blurRadius` standard deviation, transparent outside it), saturated by
// `saturation` (CSS saturate(): 1 leaves the colours as they are), and its
// middle `size` kept, which cuts away most of the blur's faded rim. A null image for
// a null cover or an empty size.
struct CoverWashOptions {
    QSize size{16, 16};
    double blurRadius = 1.2;
    double saturation = 1.5;
};

[[nodiscard]] QImage coverWash(const QImage& cover, const CoverWashOptions& options);

// What an image provider serves a wash as: the size the image asks for
// (its sourceSize), 16 x 16 when it asks for none, at most 64 each way.
[[nodiscard]] QSize coverWashSize(QSize requested);

// What a cover provider serves.
enum class CoverOutput {
    Picture, // the cover, at the size asked for
    Wash,    // its colour wash (coverWash, coverWashSize)
};

} // namespace lmp::ui
