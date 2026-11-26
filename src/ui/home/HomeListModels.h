#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QHash>
#include <QList>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "ui/models/KeyedListModel.h"

namespace lmp::ui {

// Home's sections in page order. Each names the capability it needs, so a
// mode without it leaves the section out (this phase shows the local
// library's sections only; catalogue tiers join in P08).
class HomeSectionListModel : public KeyedListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.home")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role { KeyRole = Qt::UserRole + 1, TitleRole, TierRole, CapabilityRole, KindRole, LinkRole };
    Q_ENUM(Role)

    struct Section {
        // "hero", "mixes", "listenAgain", "mostPlayed", "liked", "recentlyAdded".
        QString key;
        QString title;
        // The tier label shown before the section when it starts a tier, else empty.
        QString tier;
        // "local": the songs on this PC, which every mode has; "catalogue":
        // the account service's catalogue.
        QString capability;
        // How the section is drawn: "hero", "mixes", "songs" (a song shelf),
        // "catalog" (a shelf of catalogue cards), "charts" (chart cards) or
        // "status" (the catalogue's loading, error or cached notice).
        QString kind;
        // The section offers "See all".
        bool link = false;
        friend bool operator==(const Section&, const Section&) = default;
    };

    explicit HomeSectionListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const { return static_cast<int>(m_sections.size()); }

    // Replaces the list only when it changed, so QML keeps its sections.
    void setSections(QList<Section> sections);

signals:
    void countChanged();

private:
    QList<Section> m_sections;
};

// The generated mixes as cards (home.ts mixCard): title, badge, the line
// under it, the two gradient colours and whether the mix has songs.
class HomeMixListModel : public KeyedListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.home")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role {
        MixIdRole = Qt::UserRole + 1,
        TitleRole,
        BadgeRole,
        DescriptionRole,
        ColorStartRole,
        ColorEndRole,
        EmptyRole,
    };
    Q_ENUM(Role)

    struct Card {
        QString id;
        QString title;
        QString badge;
        QString description;
        QColor colorStart;
        QColor colorEnd;
        bool empty = true;
        friend bool operator==(const Card&, const Card&) = default;
    };

    explicit HomeMixListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const { return static_cast<int>(m_cards.size()); }

    void setCards(QList<Card> cards);
    [[nodiscard]] QString idAt(int row) const;

signals:
    void countChanged();

private:
    QList<Card> m_cards;
};

} // namespace lmp::ui
