#pragma once

#include "core/catalog/CatalogModels.h"

#include <QString>

#include <cstdint>
#include <optional>

// How a chart card looks when the catalogue sends no picture for it: a drawn
// cover chosen deterministically from the chart. City charts show a skyline
// (a few cities have their own landmark), genre charts a motif matched to the
// genre with a palette to match (else an abstract cover and a palette seeded
// from the chart's identity), and the region's own chart a fixed cover.
// Pure functions, no I/O.
namespace lmp::core::catalog {

enum class ChartFamily {
    Global,
    City,
    Genre,
};

struct GenreArt {
    // One of the drawn covers ("marquee", "lotus", ...).
    QString motif;
    // The palette meant for it ("bollywood", "devotional", ...).
    QString tone;

    friend bool operator==(const GenreArt&, const GenreArt&) = default;
};

struct ChartVisual {
    // "<region>:<kind>:<type>:<id>", the chart's navigation identity.
    QString identity;
    ChartFamily family = ChartFamily::Global;
    // The drawn cover: "region", "genre-<motif>", "genre-0".."genre-2", or a
    // city landmark ("mumbai", ..., "generic").
    QString visualKey;
    int paletteIndex = 0;
    int patternIndex = 0;
    // "City chart", or "Top songs" and the like.
    QString kindLabel;
    // The palette by name for a recognised genre or the region chart; empty
    // when the seeded palette (paletteIndex) is used.
    QString tone;

    friend bool operator==(const ChartVisual&, const ChartVisual&) = default;
};

struct ChartPalette {
    QString background1;
    QString background2;
    QString accent;
    QString accent2;

    friend bool operator==(const ChartPalette&, const ChartPalette&) = default;
};

[[nodiscard]] QString chartIdentity(const ChartRef& chart);
// The 32-bit FNV-1a hash of the UTF-16 units, as the reference computes it.
[[nodiscard]] std::uint32_t chartStableHash(const QString& value);
// Lower case, accents dropped, every run of other characters one space.
[[nodiscard]] QString normalizeChartName(const QString& value);
// The cover and palette a genre name stands for: the whole name first, then
// the most specific word in it; nothing for a genre no cover fits.
[[nodiscard]] std::optional<GenreArt> resolveGenreArt(const QString& name);
// "mumbai", "delhi", "bengaluru", "new-york", "london", "los-angeles" or
// "generic".
[[nodiscard]] QString resolveCityLandmark(const QString& name);
[[nodiscard]] ChartVisual resolveChartVisual(const ChartDescriptor& chart);
// The card's second line: a city chart's subtitle (else its title); nothing
// on genre and region cards, where it only repeated the label.
[[nodiscard]] QString chartCardSubtitle(const ChartDescriptor& chart, const ChartVisual& visual);
// The colours a visual is drawn in.
[[nodiscard]] ChartPalette chartPalette(const ChartVisual& visual);

} // namespace lmp::core::catalog
