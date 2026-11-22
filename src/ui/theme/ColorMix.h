#pragma once

#include <QColor>

namespace lmp::ui {

// CSS color-mix(in srgb, a <amountOfA>, b). Interpolates with premultiplied
// alpha, so mixing with a transparent colour keeps the other colour's channels
// and scales only its alpha.
[[nodiscard]] QColor mixSrgb(const QColor& a, const QColor& b, qreal amountOfA);

// CSS color-mix(in oklab, a <amountOfA>, b). Same premultiplied interpolation,
// done in the OKLab space. Results outside sRGB are clamped per channel.
[[nodiscard]] QColor mixOklab(const QColor& a, const QColor& b, qreal amountOfA);

} // namespace lmp::ui
