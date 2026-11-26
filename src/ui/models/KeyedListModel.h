#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QModelIndex>
#include <QSet>
#include <QString>
#include <QStringList>

#include "ui/player/QueueListDiff.h"

namespace lmp::ui {

// A list model whose rows are replaced by key: rows whose key stays keep
// their place and their delegate in the view (only what they show is
// updated), and only rows that came or went are inserted or removed. A reset
// would make every view destroy and build all of its delegates again, which
// on Home is every shelf with every card.
class KeyedListModel : public QAbstractListModel {
protected:
    using QAbstractListModel::QAbstractListModel;

    // Turns `rows` into `next`, telling the views as little as it can:
    // QueueListDiff's move or splice of the keys (`keyOf` of each row), then
    // dataChanged for kept rows that differ. A change that is neither, or
    // keys that repeat, resets the model instead.
    template <typename Row, typename KeyOf>
    void replaceRows(QList<Row>& rows, const QList<Row>& next, KeyOf keyOf)
    {
        const QStringList before = keysOf(rows, keyOf);
        const QStringList after = keysOf(next, keyOf);
        const QueueListDiff diff = unique(before) && unique(after) ? QueueListDiff::between(before, after)
                                                                   : QueueListDiff{.kind = QueueListDiff::Kind::Reset};
        switch (diff.kind) {
        case QueueListDiff::Kind::None:
            break;
        case QueueListDiff::Kind::Move: {
            // beginMoveRows names the row before which it lands, in the list
            // as it was: one further on when moving down.
            const int destination = diff.to > diff.from ? diff.to + 1 : diff.to;
            beginMoveRows(QModelIndex(), diff.from, diff.from, QModelIndex(), destination);
            rows.move(diff.from, diff.to);
            endMoveRows();
            break;
        }
        case QueueListDiff::Kind::Splice:
            for (qsizetype run = diff.removed.size() - 1; run >= 0; --run) {
                const QueueListDiff::Run& removed = diff.removed.at(run);
                beginRemoveRows(QModelIndex(), removed.first, removed.last);
                rows.remove(removed.first, removed.last - removed.first + 1);
                endRemoveRows();
            }
            for (const QueueListDiff::Run& inserted : diff.inserted) {
                beginInsertRows(QModelIndex(), inserted.first, inserted.last);
                for (int row = inserted.first; row <= inserted.last; ++row) {
                    rows.insert(row, next.at(row));
                }
                endInsertRows();
            }
            break;
        case QueueListDiff::Kind::Reset:
            beginResetModel();
            rows = next;
            endResetModel();
            return;
        }
        // The keys now match row for row; what a kept row shows may differ.
        for (int row = 0; row < rows.size(); ++row) {
            if (rows.at(row) == next.at(row)) {
                continue;
            }
            rows[row] = next.at(row);
            const QModelIndex changed = index(row);
            emit dataChanged(changed, changed);
        }
    }

private:
    template <typename Row, typename KeyOf>
    [[nodiscard]] static QStringList keysOf(const QList<Row>& rows, KeyOf keyOf)
    {
        QStringList keys;
        keys.reserve(rows.size());
        for (const Row& row : rows) {
            keys.append(keyOf(row));
        }
        return keys;
    }

    [[nodiscard]] static bool unique(const QStringList& keys)
    {
        return QSet<QString>(keys.cbegin(), keys.cend()).size() == keys.size();
    }
};

} // namespace lmp::ui
