#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QList>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

namespace lmp::ui {

// The cards of a grid: playlists, albums, artists, genres. Each card knows
// how to draw its cover (a picture, a lettered placeholder, a system
// playlist's gradient and icon, or a genre tile) and what it opens.
//
// GUI thread only.
class CollectionCardModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Created by a page's view model")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    struct Card {
        // "playlist", "system", "mix", "album", "artist" or "genre".
        QString kind;
        // What the card opens: a playlist or album key, a system playlist
        // id, an artist or genre name.
        QString key;
        QString title;
        QString subtitle;
        QUrl artworkUrl;
        QString artworkId;
        // A system playlist's gradient and icon.
        QColor gradientStart;
        QColor gradientEnd;
        QString iconName;
        // A round cover for another kind of card (a catalogue artist).
        bool round = false;
        // A mix's small capitals on its cover ("DAILY MIX").
        QString badge;
        // An account artist the listener follows (the heart on its cover).
        bool followed = false;
    };

    enum Role {
        KindRole = Qt::UserRole + 1,
        KeyRole,
        TitleRole,
        SubtitleRole,
        ArtworkUrlRole,
        ArtworkIdRole,
        ArtworkLabelRole,
        RoundRole,
        GradientStartRole,
        GradientEndRole,
        IconNameRole,
        BadgeRole,
        FollowedRole,
    };
    Q_ENUM(Role)

    explicit CollectionCardModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return static_cast<int>(m_cards.size()); }
    [[nodiscard]] Card cardAt(int row) const;

    void setCards(QList<Card> cards);

signals:
    void countChanged();

private:
    QList<Card> m_cards;
};

} // namespace lmp::ui
