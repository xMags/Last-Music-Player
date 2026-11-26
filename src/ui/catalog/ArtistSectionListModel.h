#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include "ui/models/KeyedListModel.h"

namespace lmp::ui {

// The shelves of a catalogue artist page below its top songs, one row each:
// key, title, pill and whether it opens in full ("See all"). Rows are updated
// by key, so a page keeps its built shelves and cards while the rest of its
// state (the follow answer, Show all, a share) changes.
//
// GUI thread only.
class ArtistSectionListModel : public KeyedListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.catalogArtist")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role { KeyRole = Qt::UserRole + 1, TitleRole, PillRole, LinkRole };
    Q_ENUM(Role)

    struct Section {
        QString key;
        QString title;
        QString pill;
        bool link = false;
        friend bool operator==(const Section&, const Section&) = default;
    };

    explicit ArtistSectionListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;
    [[nodiscard]] int count() const { return static_cast<int>(m_sections.size()); }
    [[nodiscard]] const QList<Section>& sections() const { return m_sections; }

    // Tells the views only what changed; the same list tells nobody.
    void setSections(const QList<Section>& sections);

signals:
    void countChanged();

private:
    QList<Section> m_sections;
};

} // namespace lmp::ui
