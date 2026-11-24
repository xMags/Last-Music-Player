#include "ui/library/CollectionCardModel.h"

#include <utility>

#include "core/model/ArtworkPlaceholder.h"

namespace lmp::ui {

CollectionCardModel::CollectionCardModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int CollectionCardModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant CollectionCardModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    const Card& card = m_cards.at(index.row());
    switch (role) {
    case KindRole:
        return card.kind;
    case KeyRole:
        return card.key;
    case TitleRole:
        return card.title;
    case SubtitleRole:
        return card.subtitle;
    case ArtworkUrlRole:
        return card.artworkUrl;
    case ArtworkIdRole:
        return card.artworkId.isEmpty() ? card.kind + QLatin1Char(':') + card.key : card.artworkId;
    case ArtworkLabelRole:
        return core::ArtworkPlaceholder::tileLabelFor(card.title);
    case RoundRole:
        return card.round || card.kind == QLatin1StringView("artist");
    case GradientStartRole:
        return card.gradientStart;
    case GradientEndRole:
        return card.gradientEnd;
    case IconNameRole:
        return card.iconName;
    case BadgeRole:
        return card.badge;
    default:
        return {};
    }
}

QHash<int, QByteArray> CollectionCardModel::roleNames() const
{
    return {
        {KindRole, "kind"},
        {KeyRole, "key"},
        {TitleRole, "title"},
        {SubtitleRole, "subtitle"},
        {ArtworkUrlRole, "artworkUrl"},
        {ArtworkIdRole, "artworkId"},
        {ArtworkLabelRole, "artworkLabel"},
        {RoundRole, "round"},
        {GradientStartRole, "gradientStart"},
        {GradientEndRole, "gradientEnd"},
        {IconNameRole, "iconName"},
        {BadgeRole, "badge"},
    };
}

CollectionCardModel::Card CollectionCardModel::cardAt(int row) const
{
    return row >= 0 && row < count() ? m_cards.at(row) : Card{};
}

void CollectionCardModel::setCards(QList<Card> cards)
{
    beginResetModel();
    m_cards = std::move(cards);
    endResetModel();
    emit countChanged();
}

} // namespace lmp::ui
