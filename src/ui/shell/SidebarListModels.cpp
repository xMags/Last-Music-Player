#include "ui/shell/SidebarListModels.h"

#include <utility>

namespace lmp::ui {

SidebarNavListModel::SidebarNavListModel(QList<Item> items, QObject* parent)
    : QAbstractListModel(parent)
    , m_items(std::move(items))
{
}

int SidebarNavListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant SidebarNavListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const Item& item = m_items.at(index.row());
    switch (role) {
    case KindRole:
        return item.kind;
    case LabelRole:
        return item.label;
    case IconNameRole:
        return item.iconName;
    default:
        return {};
    }
}

QHash<int, QByteArray> SidebarNavListModel::roleNames() const
{
    return {{KindRole, "kind"}, {LabelRole, "label"}, {IconNameRole, "iconName"}};
}

SidebarPlaylistListModel::SidebarPlaylistListModel(QObject* parent)
    : KeyedListModel(parent)
{
}

int SidebarPlaylistListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant SidebarPlaylistListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const Item& item = m_items.at(index.row());
    switch (role) {
    case PlaylistIdRole:
        return item.id;
    case NameRole:
        return item.name;
    case CoverStyleRole:
        return item.coverStyle;
    case ArtworkUrlRole:
        return item.artworkUrl;
    case CollageRole:
        return QVariant::fromValue(item.collage);
    default:
        return {};
    }
}

QHash<int, QByteArray> SidebarPlaylistListModel::roleNames() const
{
    return {{PlaylistIdRole, "playlistId"},
            {NameRole, "name"},
            {CoverStyleRole, "coverStyle"},
            {ArtworkUrlRole, "artworkUrl"},
            {CollageRole, "collage"}};
}

void SidebarPlaylistListModel::setItems(QList<Item> items)
{
    if (items == m_items) {
        return;
    }
    const int before = count();
    replaceRows(m_items, items, [](const Item& item) { return item.id; });
    if (count() != before) {
        emit countChanged();
    }
}

} // namespace lmp::ui
