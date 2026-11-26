#include "ui/images/CoverWash.h"

#include <QColor>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace lmp::ui {

namespace {

constexpr int kMargin = 2;

// One channel set of a premultiplied pixel, as real numbers.
struct Pixel {
    double r = 0;
    double g = 0;
    double b = 0;
    double a = 0;
};

[[nodiscard]] std::vector<double> gaussianKernel(double sigma)
{
    if (sigma <= 0) {
        return {1.0};
    }
    const int reach = static_cast<int>(std::ceil(sigma * 3));
    std::vector<double> weights(static_cast<std::size_t>(2 * reach + 1));
    double total = 0;
    for (int offset = -reach; offset <= reach; ++offset) {
        const double weight = std::exp(-(offset * offset) / (2 * sigma * sigma));
        weights[static_cast<std::size_t>(offset + reach)] = weight;
        total += weight;
    }
    for (double& weight : weights) {
        weight /= total;
    }
    return weights;
}

// Blurs along one axis; pixels outside the picture are transparent, as a
// canvas filter treats them.
[[nodiscard]] std::vector<Pixel> blurAxis(const std::vector<Pixel>& source, int width, int height,
                                          const std::vector<double>& kernel, bool horizontal)
{
    const int reach = static_cast<int>(kernel.size() / 2);
    std::vector<Pixel> result(source.size());
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            Pixel sum;
            for (int offset = -reach; offset <= reach; ++offset) {
                const int sx = horizontal ? x + offset : x;
                const int sy = horizontal ? y : y + offset;
                if (sx < 0 || sy < 0 || sx >= width || sy >= height) {
                    continue;
                }
                const Pixel& p = source[static_cast<std::size_t>(sy * width + sx)];
                const double w = kernel[static_cast<std::size_t>(offset + reach)];
                sum.r += p.r * w;
                sum.g += p.g * w;
                sum.b += p.b * w;
                sum.a += p.a * w;
            }
            result[static_cast<std::size_t>(y * width + x)] = sum;
        }
    }
    return result;
}

[[nodiscard]] int channel(double value)
{
    return static_cast<int>(std::lround(std::clamp(value, 0.0, 255.0)));
}

} // namespace

QSize coverWashSize(QSize requested)
{
    if (!requested.isValid() || requested.isEmpty()) {
        return {16, 16};
    }
    return {std::min(requested.width(), 64), std::min(requested.height(), 64)};
}

QImage coverWash(const QImage& cover, const CoverWashOptions& options)
{
    if (cover.isNull() || options.size.isEmpty()) {
        return {};
    }
    const int width = options.size.width() + 2 * kMargin;
    const int height = options.size.height() + 2 * kMargin;
    const QImage small = cover.convertToFormat(QImage::Format_ARGB32_Premultiplied)
                             .scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    std::vector<Pixel> pixels(static_cast<std::size_t>(width * height));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const QRgb c = small.pixel(x, y);
            pixels[static_cast<std::size_t>(y * width + x)] = {double(qRed(c)), double(qGreen(c)), double(qBlue(c)),
                                                               double(qAlpha(c))};
        }
    }
    const std::vector<double> kernel = gaussianKernel(options.blurRadius);
    pixels = blurAxis(blurAxis(pixels, width, height, kernel, true), width, height, kernel, false);

    // CSS saturate(): the Filter Effects saturate matrix. It is linear with
    // rows summing to 1, so it applies to premultiplied colour unchanged.
    const double s = options.saturation;
    const std::array<std::array<double, 3>, 3> matrix{{
        {0.213 + 0.787 * s, 0.715 - 0.715 * s, 0.072 - 0.072 * s},
        {0.213 - 0.213 * s, 0.715 + 0.285 * s, 0.072 - 0.072 * s},
        {0.213 - 0.213 * s, 0.715 - 0.715 * s, 0.072 + 0.928 * s},
    }};

    QImage wash(options.size, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < options.size.height(); ++y) {
        for (int x = 0; x < options.size.width(); ++x) {
            const Pixel& p = pixels[static_cast<std::size_t>((y + kMargin) * width + (x + kMargin))];
            const int alpha = channel(p.a);
            const auto mix = [&](const std::array<double, 3>& row) {
                return std::min(channel(row[0] * p.r + row[1] * p.g + row[2] * p.b), alpha);
            };
            wash.setPixel(x, y, qRgba(mix(matrix[0]), mix(matrix[1]), mix(matrix[2]), alpha));
        }
    }
    return wash;
}

} // namespace lmp::ui
