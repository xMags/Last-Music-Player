#include "ui/player/QueueListModel.h"

#include "core/net/RemoteUrlPolicy.h"
#include "core/model/DisplayTitle.h"

#include <QHash>

#include "core/playback/QueueTrackIdentity.h"
#include "ui/player/CoverSource.h"
#include "ui/player/PlayerBarViewModel.h"
#include "ui/player/QueueListDiff.h"

namespace lmp::ui {

namespace {

// The length as the rows show it: from the seconds when known, otherwise the
// stored text, otherwise 0:00 (as unknown lengths show in the design).
QString durationTextOf(const core::Track& track)
{
    if (track.durationSeconds > 0 || track.durationText.isEmpty()) {
        return PlayerBarViewModel::formatTime(track.durationSeconds);
    }
    return track.durationText;
}

} // namespace

QueueListModel::QueueListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int QueueListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant QueueListModel::data(const QModelIndex& index, int role) const
{
    if (!checkIndex(index, CheckIndexOption::IndexIsValid | CheckIndexOption::ParentIsInvalid)) {
        return {};
    }
    const Row& row = m_rows.at(index.row());
    switch (role) {
    case RowKeyRole:
        return row.key;
    case TitleRole:
        return row.title;
    case ArtistLineRole:
        return row.artistLine;
    case ArtworkUrlRole:
        return row.artworkUrl;
    case ArtworkIdRole:
        return row.artworkId;
    case DurationTextRole:
        return row.durationText;
    case QueuedRole:
        return row.queued;
    case RemoteUrlRole:
        return row.remoteUrl;
    default:
        return {};
    }
}

QHash<int, QByteArray> QueueListModel::roleNames() const
{
    return {
        {RowKeyRole, "rowKey"},
        {TitleRole, "title"},
        {ArtistLineRole, "artistLine"},
        {ArtworkUrlRole, "artworkUrl"},
        {ArtworkIdRole, "artworkId"},
        {DurationTextRole, "durationText"},
        {QueuedRole, "queued"},
        {RemoteUrlRole, "remoteUrl"},
    };
}

QString QueueListModel::keyAt(int row) const
{
    return row >= 0 && row < count() ? m_rows.at(row).key : QString();
}

QStringList QueueListModel::keys() const
{
    QStringList keys;
    keys.reserve(m_rows.size());
    for (const Row& row : m_rows) {
        keys.append(row.key);
    }
    return keys;
}

QStringList QueueListModel::keysOf(const QList<core::QueueEntry>& entries)
{
    QStringList keys;
    keys.reserve(entries.size());
    QHash<QString, int> seen;
    for (const core::QueueEntry& entry : entries) {
        const QString identity = core::queueTrackIdentity(entry.track);
        int& repeats = seen[identity];
        keys.append(identity + QLatin1Char('#') + QString::number(repeats));
        ++repeats;
    }
    return keys;
}

QList<QueueListModel::Row> QueueListModel::rowsOf(const QList<core::QueueEntry>& entries)
{
    const QStringList keys = keysOf(entries);
    QList<Row> rows;
    rows.reserve(entries.size());
    for (qsizetype index = 0; index < entries.size(); ++index) {
        const core::QueueEntry& entry = entries.at(index);
        const core::Track& track = entry.track;
        const bool queued = entry.kind == core::QueueEntryKind::User;
        QString artistLine = track.artist;
        if (queued) {
            artistLine = track.artist.isEmpty() ? tr("Queued") : tr("%1 · Queued").arg(track.artist);
        }
        rows.append(Row{
            .key = keys.at(index),
            .title = core::displayTitle(track.title),
            .artistLine = artistLine,
            .artworkUrl = CoverSource::forTrack(track).displayUrl(),
            .artworkId = CoverSource::placeholderIdFor(track),
            .durationText = durationTextOf(track),
            .queued = queued,
            // The address downloads are marked by: a downloaded copy keeps
            // the one it came from (a queue saved before such copies played
            // as their account songs holds them).
            .remoteUrl = track.isRemote() || core::net::isHttpUrl(track.sourceUrl) ? track.sourceUrl : QString(),
        });
    }
    return rows;
}

void QueueListModel::setEntries(const QList<core::QueueEntry>& entries)
{
    const QList<Row> next = rowsOf(entries);
    const QStringList nextKeys = keysOf(entries);
    const int countBefore = count();
    const QueueListDiff diff = QueueListDiff::between(keys(), nextKeys);
    switch (diff.kind) {
    case QueueListDiff::Kind::None:
        break;
    case QueueListDiff::Kind::Move: {
        // beginMoveRows names the place before which the row lands, in the
        // list as it was: one further on when moving down.
        const int destination = diff.to > diff.from ? diff.to + 1 : diff.to;
        beginMoveRows(QModelIndex(), diff.from, diff.from, QModelIndex(), destination);
        m_rows.move(diff.from, diff.to);
        endMoveRows();
        break;
    }
    case QueueListDiff::Kind::Splice:
        for (qsizetype run = diff.removed.size() - 1; run >= 0; --run) {
            const QueueListDiff::Run& removed = diff.removed.at(run);
            beginRemoveRows(QModelIndex(), removed.first, removed.last);
            m_rows.remove(removed.first, removed.last - removed.first + 1);
            endRemoveRows();
        }
        for (const QueueListDiff::Run& inserted : diff.inserted) {
            beginInsertRows(QModelIndex(), inserted.first, inserted.last);
            for (int row = inserted.first; row <= inserted.last; ++row) {
                m_rows.insert(row, next.at(row));
            }
            endInsertRows();
        }
        break;
    case QueueListDiff::Kind::Reset:
        beginResetModel();
        m_rows = next;
        endResetModel();
        break;
    }
    refreshChangedRows(next);
    if (count() != countBefore) {
        emit countChanged();
    }
}

void QueueListModel::refreshChangedRows(const QList<Row>& next)
{
    // After the structural changes the keys match row for row; what a kept
    // row shows may still differ (a song moved into the user queue).
    for (int row = 0; row < count(); ++row) {
        if (m_rows.at(row) == next.at(row)) {
            continue;
        }
        m_rows[row] = next.at(row);
        const QModelIndex changed = index(row);
        emit dataChanged(changed, changed);
    }
}

} // namespace lmp::ui
