#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace lmp::ui {

// The items of one action menu. A view model fills it before the menu opens;
// the menu reports the chosen item's id back.
class MenuItemListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role {
        ItemIdRole = Qt::UserRole + 1,
        LabelRole,
        IconNameRole,
        EnabledRole,
        DangerRole,
        SeparatorBeforeRole,
    };
    Q_ENUM(Role)

    struct Item {
        QString id;
        QString label;
        QString iconName;
        bool enabled = true;
        bool danger = false;
        // Starts a new group: a divider is drawn above it, except on the first item.
        bool separatorBefore = false;
    };

    explicit MenuItemListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return static_cast<int>(m_items.size()); }

    // Replaces every item at once.
    void setItems(QList<Item> items);
    Q_INVOKABLE void addItem(const QString& id, const QString& label,
                             const QString& iconName = {}, bool danger = false,
                             bool separatorBefore = false, bool enabled = true);
    Q_INVOKABLE void clear();

    // Keyboard movement between enabled items, as the menu's arrow keys,
    // Home and End do it. Each returns -1 when no item is enabled.
    // next/previous wrap around; from -1 they start at the first/last item.
    Q_INVOKABLE int nextEnabled(int from) const;
    Q_INVOKABLE int previousEnabled(int from) const;
    Q_INVOKABLE int firstEnabled() const;
    Q_INVOKABLE int lastEnabled() const;
    Q_INVOKABLE QString idAt(int row) const;

signals:
    void countChanged();

private:
    [[nodiscard]] int step(int from, int direction) const;

    QList<Item> m_items;
};

} // namespace lmp::ui
