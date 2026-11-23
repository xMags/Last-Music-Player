#include "core/library/TrackActions.h"

#include <optional>

#include "core/db/CollectionRepository.h"
#include "core/db/GroupQueries.h"
#include "core/db/TrackRepository.h"
#include "core/library/UserDataOperationGate.h"
#include "core/mode/Capabilities.h"
#include "core/mode/ModeService.h"
#include "core/mode/SourceMode.h"
#include "core/model/CatalogSourceKey.h"
#include "core/playback/PlayerController.h"

namespace lmp::core {

TrackActions::TrackActions(const Dependencies& dependencies, QObject* parent)
    : QObject(parent)
    , m_player(dependencies.player)
    , m_tracks(dependencies.tracks)
    , m_collections(dependencies.collections)
    , m_groups(dependencies.groups)
    , m_mode(dependencies.mode)
    , m_gate(dependencies.gate)
{
}

bool TrackActions::canPlay(const Track& track) const
{
    if (track.unavailable) {
        return false;
    }
    return !track.isRemote() || m_mode.capabilities().streamRemoteSongs;
}

bool TrackActions::canKeep(const Track& track) const
{
    if (catalogSourceKey(track).isEmpty()) {
        return false;
    }
    // An account song's likes and playlists live on the service; they are
    // saved there once account sync exists.
    return !(track.isRemote() && m_mode.mode() == mode::SourceMode::Account);
}

bool TrackActions::play(const Track& track, const QList<Track>& context)
{
    if (!canPlay(track)) {
        return false;
    }
    if (context.isEmpty()) {
        m_player.playTrack(track);
    } else {
        m_player.playInContext(track, context);
    }
    return true;
}

bool TrackActions::playNext(const Track& track)
{
    if (!canPlay(track)) {
        return false;
    }
    m_player.playNext({track});
    return true;
}

bool TrackActions::addToQueue(const Track& track)
{
    if (!canPlay(track)) {
        return false;
    }
    m_player.addToQueue({track});
    return true;
}

Result<qint64> TrackActions::persist(const Track& track)
{
    if (track.rowId > 0) {
        return track.rowId;
    }
    const QString key = catalogSourceKey(track);
    if (key.isEmpty()) {
        return Failure::of(FailureKind::InvalidResponse, QStringLiteral("The song has no identity"));
    }
    return track.isRemote() ? m_tracks.upsertRemoteTrack(track, key) : m_tracks.upsertLocalTrack(track, key);
}

std::optional<bool> TrackActions::toggleLike(const Track& track)
{
    if (!canKeep(track)) {
        return std::nullopt;
    }
    const std::optional<UserDataOperationGate::Lease> lease = m_gate.tryEnter();
    if (!lease) {
        return std::nullopt;
    }
    const QString key = catalogSourceKey(track);
    // The song needs its row before the flag can be set on it.
    if (!persist(track)) {
        return std::nullopt;
    }
    const bool liked = !m_tracks.isLiked(key);
    if (!m_tracks.setLiked(key, liked)) {
        return std::nullopt;
    }
    emit likedChanged(key, liked);
    return liked;
}

QList<Track> TrackActions::playlists()
{
    QList<Track> mine;
    for (const Track& playlist : m_groups.loadPlaylists()) {
        // The account's playlists come with account sync.
        if (playlist.origin != QLatin1StringView("account")) {
            mine.append(playlist);
        }
    }
    return mine;
}

Result<void> TrackActions::addToPlaylist(const Track& track, const QString& playlistKey)
{
    if (!canKeep(track) || playlistKey.isEmpty()) {
        return Failure::of(FailureKind::NotAvailable);
    }
    const std::optional<UserDataOperationGate::Lease> lease = m_gate.tryEnter();
    if (!lease) {
        return Failure::of(FailureKind::Cancelled, QStringLiteral("User data is being cleaned up"));
    }
    const Result<qint64> row = persist(track);
    if (!row) {
        return row.failure();
    }
    const Result<void> added = m_collections.addTrackToPlaylist(playlistKey, row.value());
    if (added) {
        emit playlistChanged(playlistKey);
    }
    return added;
}

Result<QString> TrackActions::addToNewPlaylist(const Track& track, const QString& name)
{
    const QString title = name.trimmed();
    if (title.isEmpty() || !canKeep(track)) {
        return Failure::of(FailureKind::NotAvailable);
    }
    qint64 createdId = 0;
    {
        const std::optional<UserDataOperationGate::Lease> lease = m_gate.tryEnter();
        if (!lease) {
            return Failure::of(FailureKind::Cancelled, QStringLiteral("User data is being cleaned up"));
        }
        const Result<qint64> created = m_collections.createPlaylist(title);
        if (!created) {
            return created.failure();
        }
        createdId = created.value();
    }
    QString key;
    for (const Track& playlist : playlists()) {
        if (playlist.rowId == createdId) {
            key = playlist.sourceUrl;
            break;
        }
    }
    emit playlistsChanged();
    if (key.isEmpty()) {
        return Failure::of(FailureKind::Storage, QStringLiteral("The new playlist was not found"));
    }
    const Result<void> added = addToPlaylist(track, key);
    if (!added) {
        return added.failure();
    }
    return key;
}

Result<void> TrackActions::removeFromPlaylist(const Track& track, const QString& playlistKey)
{
    if (playlistKey.isEmpty() || track.rowId <= 0) {
        return Failure::of(FailureKind::NotAvailable);
    }
    const std::optional<UserDataOperationGate::Lease> lease = m_gate.tryEnter();
    if (!lease) {
        return Failure::of(FailureKind::Cancelled, QStringLiteral("User data is being cleaned up"));
    }
    const Result<void> removed = m_collections.removeTrackFromPlaylist(playlistKey, track.rowId);
    if (removed) {
        emit playlistChanged(playlistKey);
    }
    return removed;
}

void TrackActions::openAlbum(const Track& track)
{
    emit albumRequested(track);
}

void TrackActions::openArtist(const Track& track)
{
    emit artistRequested(track);
}

} // namespace lmp::core
