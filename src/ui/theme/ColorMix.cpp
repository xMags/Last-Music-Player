#include "ui/theme/ColorMix.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace lmp::ui {

namespace {

using Triple = std::array<double, 3>;

double clampUnit(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double srgbToLinear(double channel)
{
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

double linearToSrgb(double channel)
{
    return channel <= 0.0031308 ? channel * 12.92 : 1.055 * std::pow(channel, 1.0 / 2.4) - 0.055;
}

// Matrices from the OKLab definition (Ottosson, 2020), as used by CSS Color 4.
Triple linearSrgbToOklab(const Triple& rgb)
{
    const double l = 0.4122214708 * rgb[0] + 0.5363325363 * rgb[1] + 0.0514459929 * rgb[2];
    const double m = 0.2119034982 * rgb[0] + 0.6806995451 * rgb[1] + 0.1073969566 * rgb[2];
    const double s = 0.0883024619 * rgb[0] + 0.2817188376 * rgb[1] + 0.6299787005 * rgb[2];
    const double lc = std::cbrt(l);
    const double mc = std::cbrt(m);
    const double sc = std::cbrt(s);
    return {
        0.2104542553 * lc + 0.7936177850 * mc - 0.0040720468 * sc,
        1.9779984951 * lc - 2.4285922050 * mc + 0.4505937099 * sc,
        0.0259040371 * lc + 0.7827717662 * mc - 0.8086757660 * sc,
    };
}

Triple oklabToLinearSrgb(const Triple& lab)
{
    const double lc = lab[0] + 0.3963377774 * lab[1] + 0.2158037573 * lab[2];
    const double mc = lab[0] - 0.1055613458 * lab[1] - 0.0638541728 * lab[2];
    const double sc = lab[0] - 0.0894841775 * lab[1] - 1.2914855480 * lab[2];
    const double l = lc * lc * lc;
    const double m = mc * mc * mc;
    const double s = sc * sc * sc;
    return {
        4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
        -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
        -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s,
    };
}

Triple srgbChannels(const QColor& color)
{
    const QColor rgb = color.toRgb();
    return {rgb.redF(), rgb.greenF(), rgb.blueF()};
}

// Premultiplied interpolation of two colours already expressed in some space.
// Returns the un-premultiplied result and the mixed alpha.
struct MixedChannels {
    Triple channels;
    double alpha;
};

MixedChannels interpolate(const Triple& a, double alphaA, const Triple& b, double alphaB,
                          double amountOfA)
{
    const double weightA = clampUnit(amountOfA);
    const double weightB = 1.0 - weightA;
    const double alpha = alphaA * weightA + alphaB * weightB;
    if (alpha <= 0.0) {
        return {{0.0, 0.0, 0.0}, 0.0};
    }
    Triple mixed{};
    for (std::size_t i = 0; i < mixed.size(); ++i) {
        mixed[i] = (a[i] * alphaA * weightA + b[i] * alphaB * weightB) / alpha;
    }
    return {mixed, alpha};
}

} // namespace

QColor mixSrgb(const QColor& a, const QColor& b, qreal amountOfA)
{
    const MixedChannels result = interpolate(srgbChannels(a), a.alphaF(), srgbChannels(b),
                                             b.alphaF(), amountOfA);
    return QColor::fromRgbF(static_cast<float>(clampUnit(result.channels[0])),
                            static_cast<float>(clampUnit(result.channels[1])),
                            static_cast<float>(clampUnit(result.channels[2])),
                            static_cast<float>(result.alpha));
}

QColor mixOklab(const QColor& a, const QColor& b, qreal amountOfA)
{
    const auto toOklab = [](const QColor& color) {
        Triple linear = srgbChannels(color);
        for (double& channel : linear) {
            channel = srgbToLinear(channel);
        }
        return linearSrgbToOklab(linear);
    };
    const MixedChannels result =
        interpolate(toOklab(a), a.alphaF(), toOklab(b), b.alphaF(), amountOfA);
    const Triple linear = oklabToLinearSrgb(result.channels);
    return QColor::fromRgbF(static_cast<float>(clampUnit(linearToSrgb(clampUnit(linear[0])))),
                            static_cast<float>(clampUnit(linearToSrgb(clampUnit(linear[1])))),
                            static_cast<float>(clampUnit(linearToSrgb(clampUnit(linear[2])))),
                            static_cast<float>(result.alpha));
}

} // namespace lmp::ui
