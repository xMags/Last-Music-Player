#include "ui/dev/ThemeTokenListModel.h"

#include <QColor>
#include <QFont>
#include <QMetaObject>
#include <QMetaType>

namespace lmp::ui {

ThemeTokenListModel::ThemeTokenListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ThemeTokenListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_properties.size());
}

QVariant ThemeTokenListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)
        || m_theme.isNull()) {
        return {};
    }
    const QMetaProperty& property = m_properties.at(index.row());
    switch (role) {
    case NameRole:
        return QString::fromLatin1(property.name());
    case ValueRole:
        return property.read(m_theme.data());
    default:
        return {};
    }
}

QHash<int, QByteArray> ThemeTokenListModel::roleNames() const
{
    return {{NameRole, "name"}, {ValueRole, "value"}};
}

void ThemeTokenListModel::setTheme(Theme* theme)
{
    if (theme == m_theme) {
        return;
    }
    if (m_theme) {
        disconnect(m_theme, nullptr, this, nullptr);
    }
    m_theme = theme;
    // Every colour of the theme changes on its one `changed` signal.
    if (m_theme) {
        connect(m_theme, &ThemeColors::changed, this, &ThemeTokenListModel::refreshValues);
    }
    rebuild();
    emit themeChanged();
}

void ThemeTokenListModel::setKind(Kind kind)
{
    if (kind == m_kind) {
        return;
    }
    m_kind = kind;
    rebuild();
    emit kindChanged();
}

void ThemeTokenListModel::refreshValues()
{
    if (m_properties.isEmpty()) {
        return;
    }
    emit dataChanged(index(0), index(rowCount() - 1), {ValueRole});
}

void ThemeTokenListModel::rebuild()
{
    beginResetModel();
    m_properties.clear();
    if (m_theme) {
        const QMetaObject* meta = m_theme->metaObject();
        const QMetaType wanted =
            m_kind == Kind::Colors ? QMetaType::fromType<QColor>() : QMetaType::fromType<QFont>();
        for (int i = QObject::staticMetaObject.propertyCount(); i < meta->propertyCount(); ++i) {
            const QMetaProperty property = meta->property(i);
            if (property.metaType() == wanted) {
                m_properties.append(property);
            }
        }
    }
    endResetModel();
}

} // namespace lmp::ui
