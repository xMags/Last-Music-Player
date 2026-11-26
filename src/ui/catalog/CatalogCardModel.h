#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QtQml/qqmlregistration.h>

#include "core/catalog/CatalogModels.h"
#include "ui/models/KeyedListModel.h"

namespace lmp::ui {

// Catalogue items as cards: songs, albums, playlists (square) and artists
// (round). The items stay here so a click can act on the one shown.
class CatalogCardModel : public KeyedListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the pages")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)
    // The first card is round (an artist): a shelf still loading its cards
    // draws round placeholders.
    Q_PROPERTY(bool leadsWithRound READ leadsWithRound NOTIFY itemsChanged FINAL)

public:
    enum Role {
        TitleRole = Qt::UserRole + 1,
        SubtitleRole,
        ArtworkUrlRole,
        // The placeholder's colour key and its letter.
        ArtworkIdRole,
        ArtworkLabelRole,
        // "song", "album", "playlist" or "artist".
        KindRole,
        RoundRole,
        // A release row's "12 Mar 2024" and how many songs ("" for unknown).
        ReleaseDateRole,
        TrackCountRole,
    };
    Q_ENUM(Role)

    explicit CatalogCardModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const { return static_cast<int>(m_items.size()); }
    [[nodiscard]] bool leadsWithRound() const
    {
        return !m_items.isEmpty() && m_items.constFirst().type == core::catalog::ResourceType::Artist;
    }

    [[nodiscard]] const QList<core::catalog::CatalogItem>& items() const { return m_items; }
    [[nodiscard]] const core::catalog::CatalogItem* itemAt(int row) const;
    void setItems(QList<core::catalog::CatalogItem> items);
    // The songs among the items that can play, in order.
    [[nodiscard]] QList<core::Track> tracks() const;

signals:
    void countChanged();
    // After setItems changed the items. Rows are inserted, removed and
    // updated by key, so a reset no longer marks every change.
    void itemsChanged();

private:
    QList<core::catalog::CatalogItem> m_items;
};

// Chart descriptors as cards.
class ChartCardModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the pages")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role {
        NameRole = Qt::UserRole + 1,
        TitleRole,
        SubtitleRole,
        ArtworkUrlRole,
        // "global", "city" or "genre".
        KindRole,
        RegionRole,
        ChartIdRole,
        // The drawn cover ("image://chartart/..."), shown under any picture.
        ArtSourceRole,
        // "city", "genre" or "global"; the label line ("Top songs", "City
        // chart"), its colour, and the card's second line.
        FamilyRole,
        KindLabelRole,
        KindColorRole,
        CardSubtitleRole,
    };
    Q_ENUM(Role)

    explicit ChartCardModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const { return static_cast<int>(m_charts.size()); }

    [[nodiscard]] const core::catalog::ChartDescriptor* chartAt(int row) const;
    void setCharts(QList<core::catalog::ChartDescriptor> charts);

signals:
    void countChanged();

private:
    QList<core::catalog::ChartDescriptor> m_charts;
};

} // namespace lmp::ui
