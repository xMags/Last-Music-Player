#include "ui/home/HomeListModels.h"

#include <utility>

namespace lmp::ui {

HomeSectionListModel::HomeSectionListModel(QObject* parent)
    : KeyedListModel(parent)
{
}

int HomeSectionListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant HomeSectionListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const Section& section = m_sections.at(index.row());
    switch (role) {
    case KeyRole:
        return section.key;
    case TitleRole:
        return section.title;
    case TierRole:
        return section.tier;
    case CapabilityRole:
        return section.capability;
    case KindRole:
        return section.kind;
    case LinkRole:
        return section.link;
    default:
        return {};
    }
}

QHash<int, QByteArray> HomeSectionListModel::roleNames() const
{
    return {{KeyRole, "key"},
            {TitleRole, "title"},
            {TierRole, "tier"},
            {CapabilityRole, "capability"},
            {KindRole, "kind"},
            {LinkRole, "link"}};
}

void HomeSectionListModel::setSections(QList<Section> sections)
{
    if (sections == m_sections) {
        return;
    }
    const int before = count();
    replaceRows(m_sections, sections, [](const Section& section) { return section.key; });
    if (count() != before) {
        emit countChanged();
    }
}

HomeMixListModel::HomeMixListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int HomeMixListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant HomeMixListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const Card& card = m_cards.at(index.row());
    switch (role) {
    case MixIdRole:
        return card.id;
    case TitleRole:
        return card.title;
    case BadgeRole:
        return card.badge;
    case DescriptionRole:
        return card.description;
    case ColorStartRole:
        return card.colorStart;
    case ColorEndRole:
        return card.colorEnd;
    case EmptyRole:
        return card.empty;
    default:
        return {};
    }
}

QHash<int, QByteArray> HomeMixListModel::roleNames() const
{
    return {{MixIdRole, "mixId"},           {TitleRole, "title"},       {BadgeRole, "badge"},
            {DescriptionRole, "description"}, {ColorStartRole, "colorStart"}, {ColorEndRole, "colorEnd"},
            {EmptyRole, "empty"}};
}

void HomeMixListModel::setCards(QList<Card> cards)
{
    const int before = count();
    beginResetModel();
    m_cards = std::move(cards);
    endResetModel();
    if (count() != before) {
        emit countChanged();
    }
}

QString HomeMixListModel::idAt(int row) const
{
    return row >= 0 && row < count() ? m_cards.at(row).id : QString();
}

} // namespace lmp::ui
