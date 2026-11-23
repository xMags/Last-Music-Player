#include "ui/catalog/CatalogCardModel.h"

#include "core/model/ArtworkPlaceholder.h"
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
    : QAbstractListModel(parent)
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
    case ArtworkLabelRole:
        return core::ArtworkPlaceholder::tileLabelFor(item->title);
    case KindRole:
        return core::catalog::resourceTypeName(item->type);
    case RoundRole:
        return item->type == core::catalog::ResourceType::Artist;
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
            {ArtworkLabelRole, "artworkLabel"},
            {KindRole, "kind"},
            {RoundRole, "round"}};
}

const core::catalog::CatalogItem* CatalogCardModel::itemAt(int row) const
{
    return row >= 0 && row < m_items.size() ? &m_items.at(row) : nullptr;
}

void CatalogCardModel::setItems(QList<core::catalog::CatalogItem> items)
{
    const bool countChange = items.size() != m_items.size();
    beginResetModel();
    m_items = std::move(items);
    endResetModel();
    if (countChange) {
        emit countChanged();
    }
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
    default:
        return {};
    }
}

QHash<int, QByteArray> ChartCardModel::roleNames() const
{
    return {{NameRole, "name"},       {TitleRole, "title"},   {SubtitleRole, "subtitle"}, {ArtworkUrlRole, "artworkUrl"},
            {KindRole, "chartKind"},  {RegionRole, "region"}, {ChartIdRole, "chartId"}};
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
