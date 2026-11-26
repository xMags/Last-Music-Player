#include "ui/catalog/CatalogCardModel.h"

#include "core/catalog/CatalogPresentation.h"

#include "core/catalog/ChartArt.h"
#include "ui/images/ChartArtImageProvider.h"

#include <QColor>
#include "ui/player/CoverSource.h"

namespace lmp::ui {

namespace {

[[nodiscard]] QUrl coverFor(const QString& artworkUrl)
{
    if (artworkUrl.isEmpty()) {
        return {};
    }
    core::Track cover;
    cover.artworkUrl = artworkUrl;
    cover.sourceKind = QStringLiteral("remote");
    return CoverSource::forTrack(cover).displayUrl();
}

} // namespace

CatalogCardModel::CatalogCardModel(QObject* parent)
    : KeyedListModel(parent)
{
}

int CatalogCardModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant CatalogCardModel::data(const QModelIndex& index, int role) const
{
    const core::catalog::CatalogItem* item = itemAt(index.row());
    if (!item || !index.isValid()) {
        return {};
    }
    switch (role) {
    case TitleRole:
        return item->title;
    case SubtitleRole:
        return item->subtitle;
    case ArtworkUrlRole:
        return coverFor(item->artworkUrl);
    case ArtworkIdRole:
        return core::catalog::resourceTypeName(item->type) + QLatin1Char(':') + item->title;
    case KindRole:
        return core::catalog::resourceTypeName(item->type);
    case RoundRole:
        return item->type == core::catalog::ResourceType::Artist;
    case ReleaseDateRole:
        return core::catalog::releaseDateLabel(item->releaseDate);
    case TrackCountRole:
        return item->trackCount > 0 ? QString::number(item->trackCount) : QString();
    default:
        return {};
    }
}

QHash<int, QByteArray> CatalogCardModel::roleNames() const
{
    return {{TitleRole, "title"},
            {SubtitleRole, "subtitle"},
            {ArtworkUrlRole, "artworkUrl"},
            {ArtworkIdRole, "artworkId"},
            {KindRole, "kind"},
            {RoundRole, "round"}};
}

const core::catalog::CatalogItem* CatalogCardModel::itemAt(int row) const
{
    return row >= 0 && row < m_items.size() ? &m_items.at(row) : nullptr;
}

void CatalogCardModel::setItems(QList<core::catalog::CatalogItem> items)
{
    if (items == m_items) {
        return;
    }
    const bool countChange = items.size() != m_items.size();
    // A card is the same card while it names the same thing.
    replaceRows(m_items, items, [](const core::catalog::CatalogItem& item) {
        return core::catalog::resourceTypeName(item.type) + QLatin1Char('|') + item.catalogId + QLatin1Char('|')
               + item.sourceUrl + QLatin1Char('|') + item.externalPlaylistId;
    });
    if (countChange) {
        emit countChanged();
    }
    emit itemsChanged();
}

QList<core::Track> CatalogCardModel::tracks() const
{
    QList<core::Track> songs;
    for (const core::catalog::CatalogItem& item : m_items) {
        if (item.track) {
            songs.append(*item.track);
        }
    }
    return songs;
}

ChartCardModel::ChartCardModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ChartCardModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_charts.size());
}

QVariant ChartCardModel::data(const QModelIndex& index, int role) const
{
    const core::catalog::ChartDescriptor* chart = chartAt(index.row());
    if (!chart || !index.isValid()) {
        return {};
    }
    switch (role) {
    case NameRole:
        return chart->name;
    case TitleRole:
        return chart->title;
    case SubtitleRole:
        return chart->subtitle;
    case ArtworkUrlRole:
        return coverFor(chart->artworkUrl);
    case KindRole:
        return core::catalog::chartKindName(chart->ref.kind);
    case RegionRole:
        return chart->ref.region;
    case ChartIdRole:
        return chart->ref.id;
    case ArtSourceRole:
        return ChartArtImageProvider::sourceFor(core::catalog::resolveChartVisual(*chart));
    case FamilyRole: {
        const core::catalog::ChartFamily family = core::catalog::resolveChartVisual(*chart).family;
        return family == core::catalog::ChartFamily::City  ? QStringLiteral("city")
            : family == core::catalog::ChartFamily::Genre ? QStringLiteral("genre")
                                                          : QStringLiteral("global");
    }
    case KindLabelRole:
        return core::catalog::resolveChartVisual(*chart).kindLabel;
    case KindColorRole: {
        // The accent mixed 70 percent with white.
        const QColor accent(core::catalog::chartPalette(core::catalog::resolveChartVisual(*chart)).accent);
        return QColor::fromRgbF(accent.redF() * 0.7F + 0.3F, accent.greenF() * 0.7F + 0.3F,
                                accent.blueF() * 0.7F + 0.3F);
    }
    case CardSubtitleRole:
        return core::catalog::chartCardSubtitle(*chart, core::catalog::resolveChartVisual(*chart));
    default:
        return {};
    }
}

QHash<int, QByteArray> ChartCardModel::roleNames() const
{
    return {{NameRole, "name"},
            {TitleRole, "title"},
            {SubtitleRole, "subtitle"},
            {ArtworkUrlRole, "artworkUrl"},
            {KindRole, "chartKind"},
            {RegionRole, "region"},
            {ChartIdRole, "chartId"},
            {ArtSourceRole, "artSource"},
            {FamilyRole, "family"},
            {KindLabelRole, "kindLabel"},
            {KindColorRole, "kindColor"},
            {CardSubtitleRole, "cardSubtitle"}};
}

const core::catalog::ChartDescriptor* ChartCardModel::chartAt(int row) const
{
    return row >= 0 && row < m_charts.size() ? &m_charts.at(row) : nullptr;
}

void ChartCardModel::setCharts(QList<core::catalog::ChartDescriptor> charts)
{
    const bool countChange = charts.size() != m_charts.size();
    beginResetModel();
    m_charts = std::move(charts);
    endResetModel();
    if (countChange) {
        emit countChanged();
    }
}

} // namespace lmp::ui
