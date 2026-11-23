#include "core/catalog/CatalogPresentation.h"

#include <algorithm>

namespace lmp::core::catalog {

QList<ChartSection> buildChartSections(const Discovery& discovery)
{
    ChartSection explore{QStringLiteral("explore"), QStringLiteral("Top charts"), {}, false};
    ChartSection cities{QStringLiteral("cities"), QStringLiteral("Top 25 by city"), {}, false};
    for (const ChartDescriptor& chart : discovery.charts) {
        (chart.ref.kind == ChartKind::City ? cities : explore).charts.append(chart);
    }
    if (discovery.cityChartGroups) {
        const CityChartGroups& groups = *discovery.cityChartGroups;
        cities.charts = groups.indian.charts + groups.international.charts;
        cities.partial = groups.indian.partial || groups.international.partial;
    }
    QList<ChartSection> sections;
    if (!explore.charts.isEmpty()) {
        sections.append(std::move(explore));
    }
    if (!cities.charts.isEmpty()) {
        sections.append(std::move(cities));
    }
    return sections;
}

qsizetype chartPreviewCount(qsizetype total)
{
    return std::clamp<qsizetype>(total, 0, 18);
}

qsizetype moodPreviewCount(qsizetype total)
{
    return std::clamp<qsizetype>(total, 0, 12);
}

bool chartSectionShowsSeeAll(qsizetype total)
{
    return total > 4;
}

void applyAlbumArtworkToTracks(Detail& detail)
{
    if (detail.item.type != ResourceType::Album || detail.item.artworkUrl.isEmpty()) {
        return;
    }
    for (Track& track : detail.tracks) {
        track.artworkUrl = detail.item.artworkUrl;
    }
}

} // namespace lmp::core::catalog
