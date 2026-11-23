#include "core/catalog/CatalogModels.h"

#include "core/text/CaseFold.h"

#include <QLatin1StringView>

#include <algorithm>

namespace lmp::core::catalog {

namespace {

[[nodiscard]] bool isAsciiLower(QChar character)
{
    return character >= QLatin1Char('a') && character <= QLatin1Char('z');
}

[[nodiscard]] bool isIdCharacter(QChar character)
{
    return isAsciiLower(character) || (character >= QLatin1Char('A') && character <= QLatin1Char('Z'))
        || (character >= QLatin1Char('0') && character <= QLatin1Char('9')) || character == QLatin1Char('_')
        || character == QLatin1Char('-');
}

[[nodiscard]] bool containsFolded(const QStringList& values, QStringView value)
{
    const QString folded = foldCaseAscii(value.trimmed().toString());
    if (folded.isEmpty()) {
        return false;
    }
    return std::any_of(values.cbegin(), values.cend(),
                       [&folded](const QString& entry) { return foldCaseAscii(entry.trimmed()) == folded; });
}

} // namespace

ResourceType parseResourceType(QStringView value)
{
    // Items use the singular, chart answers the plural.
    if (value == QLatin1StringView("song") || value == QLatin1StringView("songs")) {
        return ResourceType::Song;
    }
    if (value == QLatin1StringView("album") || value == QLatin1StringView("albums")) {
        return ResourceType::Album;
    }
    if (value == QLatin1StringView("playlist") || value == QLatin1StringView("playlists")) {
        return ResourceType::Playlist;
    }
    if (value == QLatin1StringView("artist") || value == QLatin1StringView("artists")) {
        return ResourceType::Artist;
    }
    return ResourceType::Unknown;
}

QString resourceTypeName(ResourceType type)
{
    switch (type) {
    case ResourceType::Song:
        return QStringLiteral("song");
    case ResourceType::Album:
        return QStringLiteral("album");
    case ResourceType::Playlist:
        return QStringLiteral("playlist");
    case ResourceType::Artist:
        return QStringLiteral("artist");
    case ResourceType::Unknown:
        break;
    }
    return {};
}

QString resourceTypePlural(ResourceType type)
{
    const QString name = resourceTypeName(type);
    return name.isEmpty() ? name : name + QLatin1Char('s');
}

ChartKind parseChartKind(QStringView value)
{
    if (value == QLatin1StringView("global")) {
        return ChartKind::Global;
    }
    if (value == QLatin1StringView("city")) {
        return ChartKind::City;
    }
    if (value == QLatin1StringView("genre")) {
        return ChartKind::Genre;
    }
    return ChartKind::Unknown;
}

QString chartKindName(ChartKind kind)
{
    switch (kind) {
    case ChartKind::Global:
        return QStringLiteral("global");
    case ChartKind::City:
        return QStringLiteral("city");
    case ChartKind::Genre:
        return QStringLiteral("genre");
    case ChartKind::Unknown:
        break;
    }
    return {};
}

SourceTags SourceTags::fromConfig(const PrivateConfig& config)
{
    return SourceTags{config.sourceTagCatalog, config.sourceTagsStream, config.sourceLabels,
                      config.externalPlaylistIdPrefix};
}

bool SourceTags::isCatalogTag(QStringView tag) const
{
    return !catalog.isEmpty() && foldCaseAscii(tag.trimmed().toString()) == foldCaseAscii(catalog);
}

bool SourceTags::isStreamTag(QStringView tag) const
{
    const QString trimmed = tag.trimmed().toString();
    return !trimmed.isEmpty() && stream.contains(trimmed);
}

bool SourceTags::isSourceLabel(QStringView value) const
{
    return containsFolded(labels, value);
}

bool SourceTags::isExternalPlaylistId(QStringView value) const
{
    if (externalPlaylistPrefix.isEmpty() || !value.startsWith(externalPlaylistPrefix)) {
        return false;
    }
    const QStringView rest = value.mid(externalPlaylistPrefix.size());
    return rest.size() >= 10 && rest.size() <= 80 && std::all_of(rest.begin(), rest.end(), isIdCharacter);
}

bool isValidRegionCode(QStringView code)
{
    return code.size() == 2 && isAsciiLower(code.at(0)) && isAsciiLower(code.at(1));
}

std::optional<QString> normalizeRegionCode(QStringView value)
{
    const QString code = foldCaseAscii(value.trimmed().toString());
    return isValidRegionCode(code) ? std::optional<QString>(code) : std::nullopt;
}

bool isValidChartRef(const ChartRef& chart)
{
    if (!isValidRegionCode(chart.region) || chart.kind == ChartKind::Unknown
        || chart.resourceType == ResourceType::Unknown || chart.resourceType == ResourceType::Artist
        || chart.id.isEmpty() || chart.id.size() > 160
        || !std::all_of(chart.id.cbegin(), chart.id.cend(),
                        [](QChar character) { return isIdCharacter(character) || character == QLatin1Char('.'); })) {
        return false;
    }
    if (chart.kind != ChartKind::Global) {
        return chart.resourceType == ResourceType::Song;
    }
    return chart.id == QStringLiteral("top-") + resourceTypePlural(chart.resourceType);
}

QString artistSectionId(ArtistSection section)
{
    switch (section) {
    case ArtistSection::TopSongs:
        return QStringLiteral("top-songs");
    case ArtistSection::Albums:
        return QStringLiteral("albums");
    case ArtistSection::Singles:
        return QStringLiteral("singles");
    case ArtistSection::SimilarArtists:
        return QStringLiteral("similar-artists");
    }
    return {};
}

std::optional<ArtistSection> parseArtistSectionId(QStringView id)
{
    for (const ArtistSection section :
         {ArtistSection::TopSongs, ArtistSection::Albums, ArtistSection::Singles, ArtistSection::SimilarArtists}) {
        if (id == artistSectionId(section)) {
            return section;
        }
    }
    return std::nullopt;
}

} // namespace lmp::core::catalog
