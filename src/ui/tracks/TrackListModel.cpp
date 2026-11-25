#include "ui/tracks/TrackListModel.h"

#include "core/home/PlayedWhen.h"

#include <QDateTime>

#include <algorithm>
#include <utility>

#include "core/model/ArtworkPlaceholder.h"
#include "core/model/CatalogSourceKey.h"
#include "core/model/DisplayTitle.h"
#include "ui/player/CoverSource.h"

namespace lmp::ui {

TrackListModel::TrackListModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int TrackListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count();
}

QVariant TrackListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    const core::Track& track = m_tracks.at(index.row());
    switch (role) {
    case TitleRole:
        return core::displayTitle(track.title);
    case ArtistRole:
        return track.artist;
    case AlbumRole:
        return core::displayAlbumTitle(track.album);
    case DurationTextRole:
        return track.unavailable ? QString() : track.durationText;
    case ArtworkUrlRole:
        return CoverSource::forLibraryTrack(track).displayUrl();
    case ArtworkIdRole:
        return CoverSource::placeholderIdFor(track);
    case ArtworkLabelRole:
        return core::ArtworkPlaceholder::tileLabelFor(track.title);
    case LikedRole:
        return track.liked;
    case PlayingRole:
        return !m_playingKey.isEmpty() && m_keys.at(index.row()) == m_playingKey;
    case UnavailableRole:
        return track.unavailable;
    case UnavailableReasonRole:
        return track.unavailableReason.isEmpty() ? tr("This song no longer exists.") : track.unavailableReason;
    case NumberRole:
        return index.row() + 1;
    case SourceKeyRole:
        return m_keys.at(index.row());
    case RemoteUrlRole:
        return track.isRemote() ? track.sourceUrl : QString();
    case PlayedWhenRole:
        return core::home::relativePlayedText(track.lastPlayedAt.isEmpty() ? track.addedAt : track.lastPlayedAt,
                                              QDateTime::currentDateTimeUtc());
    case SelectedRole:
        return m_selected.contains(index.row());
    default:
        return {};
    }
}

QHash<int, QByteArray> TrackListModel::roleNames() const
{
    return {
        {TitleRole, "title"},
        {ArtistRole, "artist"},
        {AlbumRole, "album"},
        {DurationTextRole, "durationText"},
        {ArtworkUrlRole, "artworkUrl"},
        {ArtworkIdRole, "artworkId"},
        {ArtworkLabelRole, "artworkLabel"},
        {LikedRole, "liked"},
        {PlayingRole, "playing"},
        {UnavailableRole, "unavailable"},
        {UnavailableReasonRole, "unavailableReason"},
        {NumberRole, "number"},
        {SourceKeyRole, "sourceKey"},
        {SelectedRole, "selected"},
        {PlayedWhenRole, "playedWhen"},
        {RemoteUrlRole, "remoteUrl"},
    };
}

bool TrackListModel::canFetchMore(const QModelIndex& parent) const
{
    return !parent.isValid() && m_source && count() < m_total;
}

void TrackListModel::fetchMore(const QModelIndex& parent)
{
    if (canFetchMore(parent)) {
        appendPage();
    }
}

void TrackListModel::setTracks(QList<core::Track> tracks)
{
    beginResetModel();
    m_source.reset();
    m_selected.clear();
    m_tracks = std::move(tracks);
    m_total = count();
    reindex();
    endResetModel();
    emit countChanged();
}

void TrackListModel::setSource(std::shared_ptr<TrackPageSource> source)
{
    beginResetModel();
    m_source = std::move(source);
    m_selected.clear();
    m_tracks.clear();
    m_total = m_source ? std::max(0, m_source->count()) : 0;
    reindex();
    endResetModel();
    if (m_source && m_total > 0) {
        appendPage();
    }
    emit countChanged();
}

void TrackListModel::fetchAll()
{
    while (canFetchMore({})) {
        const int before = count();
        appendPage();
        if (count() == before) {
            break;
        }
    }
}

void TrackListModel::appendPage()
{
    const QList<core::Track> page = m_source->page(count(), PageSize);
    if (page.isEmpty()) {
        // The list shrank since it was counted: stop asking.
        m_total = count();
        emit countChanged();
        return;
    }
    const int first = count();
    beginInsertRows({}, first, first + static_cast<int>(page.size()) - 1);
    for (const core::Track& track : page) {
        const QString key = core::catalogSourceKey(track);
        m_rowsByKey[key].append(static_cast<int>(m_tracks.size()));
        m_keys.append(key);
        m_tracks.append(track);
    }
    endInsertRows();
    emit countChanged();
}

void TrackListModel::reindex()
{
    m_keys.clear();
    m_rowsByKey.clear();
    m_keys.reserve(m_tracks.size());
    for (int row = 0; row < count(); ++row) {
        const QString key = core::catalogSourceKey(m_tracks.at(row));
        m_keys.append(key);
        m_rowsByKey[key].append(row);
    }
}

core::Track TrackListModel::trackAt(int row) const
{
    return row >= 0 && row < count() ? m_tracks.at(row) : core::Track{};
}

void TrackListModel::setPlayingKey(const QString& sourceKey)
{
    if (sourceKey == m_playingKey) {
        return;
    }
    const QList<int> before = m_rowsByKey.value(m_playingKey);
    m_playingKey = sourceKey;
    const QList<int> after = m_rowsByKey.value(m_playingKey);
    for (const QList<int>& rows : {before, after}) {
        for (const int row : rows) {
            emit dataChanged(index(row), index(row), {PlayingRole});
        }
    }
}

void TrackListModel::setLiked(const QString& sourceKey, bool liked)
{
    for (const int row : m_rowsByKey.value(sourceKey)) {
        if (m_tracks[row].liked != liked) {
            m_tracks[row].liked = liked;
            emit dataChanged(index(row), index(row), {LikedRole});
        }
    }
}

void TrackListModel::setSelectedRows(const QList<int>& rows)
{
    QSet<int> next;
    for (const int row : rows) {
        if (row >= 0 && row < count()) {
            next.insert(row);
        }
    }
    const QSet<int> changed = (next - m_selected) + (m_selected - next);
    m_selected = std::move(next);
    for (const int row : changed) {
        emit dataChanged(index(row), index(row), {SelectedRole});
    }
}

bool TrackListModel::moveRow(int from, int to)
{
    if (from < 0 || from >= count() || to < 0 || to >= count() || canFetchMore({})) {
        return false;
    }
    if (from == to) {
        return true;
    }
    // Qt's destination is the row the moved one lands before, counted
    // before the move.
    if (!beginMoveRows({}, from, from, {}, to > from ? to + 1 : to)) {
        return false;
    }
    // A move ends the selection; every row that showed as selected is told,
    // not only those between the two places.
    const QSet<int> wasSelected = std::exchange(m_selected, {});
    m_tracks.move(from, to);
    reindex();
    endMoveRows();
    const int first = std::min(from, to);
    const int last = std::max(from, to);
    emit dataChanged(index(first), index(last), {NumberRole, SelectedRole});
    for (const int row : wasSelected) {
        // Rows outside the moved span keep their place.
        if ((row < first || row > last) && row >= 0 && row < count()) {
            emit dataChanged(index(row), index(row), {SelectedRole});
        }
    }
    return true;
}

} // namespace lmp::ui
