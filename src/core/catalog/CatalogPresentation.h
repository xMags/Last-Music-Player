#pragma once

#include "core/catalog/CatalogModels.h"

#include <QList>
#include <QString>

// How catalogue data is grouped for the Browse page. Pure functions.
namespace lmp::core::catalog {

struct ChartSection {
    QString id;
    QString title;
    QList<ChartDescriptor> charts;
    bool partial = false;
};

// "Top charts" (every chart that is not a city chart) and "Top 25 by city"
// (the city charts, or the two city groups in order when discovery has
// them), each only when it has charts.
[[nodiscard]] QList<ChartSection> buildChartSections(const Discovery& discovery);

// How many charts or mood playlists a section shows before "See all".
[[nodiscard]] qsizetype chartPreviewCount(qsizetype total);
[[nodiscard]] qsizetype moodPreviewCount(qsizetype total);
// A chart section offers "See all" when it holds more than four charts.
[[nodiscard]] bool chartSectionShowsSeeAll(qsizetype total);

// The small label beside a catalogue shelf's title on artist and album
// pages (catalogShelfPill): by the shelf's id ("top-songs" is "Trending",
// "albums" "Discography", ...), else by what it lists; empty for neither.
[[nodiscard]] QString shelfPillLabel(const QString& shelfId, ResourceType type);

// An album page has one cover: every song shows it, since a song's own
// picture can be an unrelated thumbnail. Playlists and albums without a
// cover are left as they are.
void applyAlbumArtworkToTracks(Detail& detail);

} // namespace lmp::core::catalog
