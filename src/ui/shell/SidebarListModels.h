#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QtQml/qqmlregistration.h>

namespace lmp::ui {

// The sidebar's navigation items: the main views, in order.
class SidebarNavListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.sidebar")

public:
    enum Role { KindRole = Qt::UserRole + 1, LabelRole, IconNameRole };
    Q_ENUM(Role)

    struct Item {
        int kind = 0;
        QString label;
        // Base icon name; the outlined glyph is "nav-<name>", the filled one
        // for the active item "nav-<name>-active".
        QString iconName;
    };

    explicit SidebarNavListModel(QList<Item> items, QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

private:
    QList<Item> m_items;
};

// The user's playlists shown in the sidebar, after Liked Songs.
class SidebarPlaylistListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.sidebar")

public:
    enum Role { PlaylistIdRole = Qt::UserRole + 1, NameRole };
    Q_ENUM(Role)

    struct Item {
        QString id;
        QString name;
    };

    explicit SidebarPlaylistListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setItems(QList<Item> items);

private:
    QList<Item> m_items;
};

} // namespace lmp::ui
