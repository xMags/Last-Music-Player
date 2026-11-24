#include "core/catalog/CatalogPresentation.h"

#include <QHash>
#include <QRegularExpression>
#include <QLocale>
#include <QDate>

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

QString shelfPillLabel(const QString& shelfId, ResourceType type)
{
    static const QHash<QString, QString> labels{
        {QStringLiteral("top-songs"), QStringLiteral("Trending")},
        {QStringLiteral("top-albums"), QStringLiteral("Popular")},
        {QStringLiteral("top-playlists"), QStringLiteral("Curated")},
        {QStringLiteral("moods-activities"), QStringLiteral("For the moment")},
        {QStringLiteral("top-music-videos"), QStringLiteral("Watch")},
        {QStringLiteral("top-videos"), QStringLiteral("Watch")},
        {QStringLiteral("latest-release"), QStringLiteral("New")},
        {QStringLiteral("new-releases"), QStringLiteral("Albums")},
        {QStringLiteral("new-release-songs"), QStringLiteral("Songs")},
        {QStringLiteral("biggest-hits"), QStringLiteral("Hits")},
        {QStringLiteral("albums"), QStringLiteral("Discography")},
        {QStringLiteral("singles"), QStringLiteral("Releases")},
        {QStringLiteral("similar-artists"), QStringLiteral("Discover")},
        {QStringLiteral("popular-artists"), QStringLiteral("Artists")},
    };
    if (const auto found = labels.constFind(shelfId.toLower()); found != labels.constEnd()) {
        return found.value();
    }
    switch (type) {
    case ResourceType::Song:
        return QStringLiteral("Music");
    case ResourceType::Album:
        return QStringLiteral("Albums");
    case ResourceType::Playlist:
        return QStringLiteral("Collections");
    case ResourceType::Artist:
        return QStringLiteral("Artists");
    case ResourceType::Unknown:
        break;
    }
    return {};
}

QString releaseDateLabel(const QString& releaseDate)
{
    static const QRegularExpression fullDate(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}"));
    if (!fullDate.match(releaseDate).hasMatch()) {
        return releaseDate.left(4);
    }
    const QDate date = QDate::fromString(releaseDate.left(10), Qt::ISODate);
    if (!date.isValid()) {
        return releaseDate.left(4);
    }
    return QLocale(QLocale::English, QLocale::UnitedKingdom).toString(date, QStringLiteral("d MMM yyyy"));
}

bool isReleaseList(const QList<CatalogItem>& items)
{
    return !items.isEmpty() && std::all_of(items.cbegin(), items.cend(), [](const CatalogItem& item) {
        return item.type == ResourceType::Album && !item.track;
    });
}

} // namespace lmp::core::catalog
