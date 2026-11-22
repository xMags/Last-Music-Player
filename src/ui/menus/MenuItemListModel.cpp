#include "ui/menus/MenuItemListModel.h"

#include <utility>

namespace lmp::ui {

MenuItemListModel::MenuItemListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int MenuItemListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant MenuItemListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const Item& item = m_items.at(index.row());
    switch (role) {
    case ItemIdRole:
        return item.id;
    case LabelRole:
        return item.label;
    case IconNameRole:
        return item.iconName;
    case EnabledRole:
        return item.enabled;
    case DangerRole:
        return item.danger;
    case SeparatorBeforeRole:
        // A divider never leads the menu.
        return item.separatorBefore && index.row() > 0;
    default:
        return {};
    }
}

QHash<int, QByteArray> MenuItemListModel::roleNames() const
{
    return {
        {ItemIdRole, "itemId"},
        {LabelRole, "label"},
        {IconNameRole, "iconName"},
        {EnabledRole, "itemEnabled"},
        {DangerRole, "danger"},
        {SeparatorBeforeRole, "separatorBefore"},
    };
}

void MenuItemListModel::setItems(QList<Item> items)
{
    beginResetModel();
    m_items = std::move(items);
    endResetModel();
    emit countChanged();
}

void MenuItemListModel::addItem(const QString& id, const QString& label, const QString& iconName,
                                bool danger, bool separatorBefore, bool enabled)
{
    const int row = count();
    beginInsertRows({}, row, row);
    m_items.append(Item{
        .id = id,
        .label = label,
        .iconName = iconName,
        .enabled = enabled,
        .danger = danger,
        .separatorBefore = separatorBefore,
    });
    endInsertRows();
    emit countChanged();
}

void MenuItemListModel::clear()
{
    if (m_items.isEmpty()) {
        return;
    }
    beginResetModel();
    m_items.clear();
    endResetModel();
    emit countChanged();
}

int MenuItemListModel::step(int from, int direction) const
{
    const int total = count();
    if (total == 0) {
        return -1;
    }
    int row = from;
    for (int visited = 0; visited < total; ++visited) {
        if (row < 0 || row >= total) {
            row = direction > 0 ? 0 : total - 1;
        } else {
            row = (row + direction + total) % total;
        }
        if (m_items.at(row).enabled) {
            return row;
        }
    }
    return -1;
}

int MenuItemListModel::nextEnabled(int from) const
{
    return step(from, 1);
}

int MenuItemListModel::previousEnabled(int from) const
{
    return step(from, -1);
}

int MenuItemListModel::firstEnabled() const
{
    return step(-1, 1);
}

int MenuItemListModel::lastEnabled() const
{
    return step(-1, -1);
}

QString MenuItemListModel::idAt(int row) const
{
    return row >= 0 && row < count() ? m_items.at(row).id : QString();
}

} // namespace lmp::ui
