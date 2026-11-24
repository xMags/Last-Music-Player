#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include "core/playback/QueueTypes.h"

namespace lmp::ui {

// Rows of songs for the Up Next list and the idle panel's recent plays, fed
// with QueueEngine::visibleQueue() (or plain songs as context entries).
//
// Each row is keyed by its song's queue identity and how many times that
// song came before it ("identity#0"), so a row keeps its identity wherever
// it moves. setEntries() turns a new list into the fewest notifications
// (QueueListDiff): one beginMoveRows for a single move, remove and insert
// runs when the kept rows stay in order, a reset for an empty list filling
// up and for anything else; rows that kept their key but show something new
// get dataChanged.
//
// Lives on the GUI thread.
class QueueListModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by App.queueRail")

    Q_PROPERTY(int count READ count NOTIFY countChanged FINAL)

public:
    enum Role {
        RowKeyRole = Qt::UserRole + 1,
        TitleRole,
        // The artist, with " · Queued" for songs in the user queue.
        ArtistLineRole,
        ArtworkUrlRole,
        ArtworkIdRole,
        ArtworkLabelRole,
        DurationTextRole,
        // In the user queue ("play next", "add to queue").
        QueuedRole,
        // A song from the service's address, for its download mark.
        RemoteUrlRole,
    };
    Q_ENUM(Role)

    explicit QueueListModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int count() const { return static_cast<int>(m_rows.size()); }
    // The key of `row`, or an empty string outside the list.
    [[nodiscard]] QString keyAt(int row) const;
    [[nodiscard]] QStringList keys() const;

    void setEntries(const QList<core::QueueEntry>& entries);

    // The row keys a list of entries gets.
    [[nodiscard]] static QStringList keysOf(const QList<core::QueueEntry>& entries);

signals:
    void countChanged();

private:
    // What a row shows, worked out once when it arrives.
    struct Row {
        QString key;
        QString title;
        QString artistLine;
        QUrl artworkUrl;
        QString artworkId;
        QString artworkLabel;
        QString durationText;
        bool queued = false;
        QString remoteUrl;

        friend bool operator==(const Row&, const Row&) = default;
    };

    [[nodiscard]] static QList<Row> rowsOf(const QList<core::QueueEntry>& entries);
    void refreshChangedRows(const QList<Row>& next);

    QList<Row> m_rows;
};

} // namespace lmp::ui
