#include "core/library/TrackActions.h"

#include "core/library/LikedIndex.h"

#include "core/remote/TrackJson.h"

#include "core/account/AccountLibraryEditor.h"

#include <optional>

#include "core/db/CollectionRepository.h"
#include "core/db/GroupQueries.h"
#include "core/db/TrackRepository.h"
#include "core/library/UserDataOperationGate.h"
#include "core/mode/Capabilities.h"
#include "core/mode/ModeService.h"
#include "core/mode/SourceMode.h"
#include "core/model/CatalogSourceKey.h"
#include "core/net/RemoteUrlPolicy.h"
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
    , m_account(dependencies.account)
{
    if (m_account != nullptr) {
        // Account edits finish later; the views follow their results.
        connect(m_account, &account::AccountLibraryEditor::playlistsChanged, this, &TrackActions::playlistsChanged);
        connect(m_account, &account::AccountLibraryEditor::playlistChanged, this, &TrackActions::playlistChanged);
    }
}

bool TrackActions::canPlay(const Track& track) const
{
    if (track.unavailable) {
        return false;
    }
    return !track.isRemote() || m_mode.capabilities().streamRemoteSongs;
}

bool TrackActions::canStartMix(const Track& track) const
{
    return canPlay(track) && track.isRemote() && m_mode.capabilities().playRadio;
}

void TrackActions::startMix(const Track& track)
{
    if (canStartMix(track)) {
        emit mixRequested(track);
    }
}

bool TrackActions::canShare(const Track& track) const
{
    return m_mode.capabilities().createShareLinks && !remote::track_json::accountTrackJson(track).isEmpty();
}

void TrackActions::share(const Track& track)
{
    if (canShare(track)) {
        emit shareRequested(track);
    }
}

TrackActions::PageSource TrackActions::artistPageSource(const Track& track) const
{
    if (track.origin == QLatin1StringView("account")) {
        return PageSource::Catalogue;
    }
    const mode::Capabilities capabilities = m_mode.capabilities();
    return track.isRemote() && !capabilities.browseCatalogue && capabilities.openOnlineArtistPages
        ? PageSource::Provider
        : PageSource::Library;
}

TrackActions::PageSource TrackActions::albumPageSource(const Track& track) const
{
    if (track.origin == QLatin1StringView("account")) {
        return PageSource::Catalogue;
    }
    const mode::Capabilities capabilities = m_mode.capabilities();
    return track.isRemote() && !capabilities.browseCatalogue && capabilities.openOnlineAlbumPages
            && !track.album.trimmed().isEmpty()
        ? PageSource::Provider
        : PageSource::Library;
}

bool TrackActions::canKeep(const Track& track) const
{
    if (catalogSourceKey(track).isEmpty()) {
        return false;
    }
    // An account song's likes and playlists live on the service.
    if (track.isRemote() && m_mode.accountSelected()) {
        return m_account != nullptr && m_account->handles(track);
    }
    return true;
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
    emit queued(track);
    return true;
}

bool TrackActions::addToQueue(const Track& track)
{
    if (!canPlay(track)) {
        return false;
    }
    m_player.addToQueue({track});
    emit queued(track);
    return true;
}

QList<Track> TrackActions::playable(const QList<Track>& tracks) const
{
    QList<Track> list;
    list.reserve(tracks.size());
    for (const Track& track : tracks) {
        if (canPlay(track)) {
            list.append(track);
        }
    }
    return list;
}

bool TrackActions::shufflePlay(const QList<Track>& tracks)
{
    const QList<Track> list = playable(tracks);
    if (list.isEmpty()) {
        return false;
    }
    m_player.shuffleAndPlay(list);
    return true;
}

bool TrackActions::addAllToQueue(const QList<Track>& tracks)
{
    const QList<Track> list = playable(tracks);
    if (list.isEmpty()) {
        return false;
    }
    m_player.addToQueue(list);
    return true;
}

QList<qint64> TrackActions::rowIds(const QList<Track>& tracks)
{
    QList<qint64> ids;
    ids.reserve(tracks.size());
    for (const Track& track : tracks) {
        if (track.rowId <= 0) {
            return {};
        }
        ids.append(track.rowId);
    }
    return ids;
}

Result<void> TrackActions::removeAllFromPlaylist(const QList<Track>& tracks, const QString& playlistKey)
{
    if (account::AccountLibraryEditor::isAccountPlaylistKey(playlistKey)) {
        if (m_account == nullptr || !m_account->isActive() || tracks.isEmpty()) {
            return Failure::of(FailureKind::NotAvailable);
        }
        (void)m_account->removeFromPlaylist(playlistKey, tracks);
        return Result<void>::success();
    }
    const QList<qint64> ids = rowIds(tracks);
    if (playlistKey.isEmpty() || ids.isEmpty()) {
        return Failure::of(FailureKind::NotAvailable);
    }
    const std::optional<UserDataOperationGate::Lease> lease = m_gate.tryEnter();
    if (!lease) {
        return Failure::of(FailureKind::Cancelled, QStringLiteral("User data is being cleaned up"));
    }
    const Result<void> removed = m_collections.removeTracksFromPlaylist(playlistKey, ids);
    if (removed) {
        emit playlistChanged(playlistKey);
    }
    return removed;
}

Result<void> TrackActions::moveToPlaylist(const QList<Track>& tracks, const QString& fromKey, const QString& toKey)
{
    // Between the service and this PC a move would be two edits in two
    // places that cannot fail together; it is not offered.
    if (account::AccountLibraryEditor::isAccountPlaylistKey(fromKey)
        || account::AccountLibraryEditor::isAccountPlaylistKey(toKey)) {
        return Failure::of(FailureKind::NotAvailable, QStringLiteral("account playlists do not move songs"));
    }
    const QList<qint64> ids = rowIds(tracks);
    if (fromKey.isEmpty() || toKey.isEmpty() || fromKey == toKey || ids.isEmpty()) {
        return Failure::of(FailureKind::NotAvailable);
    }
    const std::optional<UserDataOperationGate::Lease> lease = m_gate.tryEnter();
    if (!lease) {
        return Failure::of(FailureKind::Cancelled, QStringLiteral("User data is being cleaned up"));
    }
    const Result<void> moved = m_collections.moveTracksBetweenPlaylists(fromKey, toKey, ids);
    if (moved) {
        emit playlistChanged(fromKey);
        emit playlistChanged(toKey);
    }
    return moved;
}

Result<void> TrackActions::reorderPlaylist(const QString& playlistKey, const QList<Track>& tracks)
{
    if (account::AccountLibraryEditor::isAccountPlaylistKey(playlistKey)) {
        if (m_account == nullptr || !m_account->isActive() || tracks.isEmpty()) {
            return Failure::of(FailureKind::NotAvailable);
        }
        (void)m_account->replacePlaylist(playlistKey, tracks);
        return Result<void>::success();
    }
    const QList<qint64> ids = rowIds(tracks);
    if (playlistKey.isEmpty() || ids.isEmpty()) {
        return Failure::of(FailureKind::NotAvailable);
    }
    const std::optional<UserDataOperationGate::Lease> lease = m_gate.tryEnter();
    if (!lease) {
        return Failure::of(FailureKind::Cancelled, QStringLiteral("User data is being cleaned up"));
    }
    const Result<void> stored = m_collections.reorderPlaylistTracks(playlistKey, ids);
    if (stored) {
        emit playlistChanged(playlistKey);
    }
    return stored;
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

bool TrackActions::isLiked(const Track& track)
{
    return likedLookup()(track);
}

std::function<bool(const Track&)> TrackActions::likedLookup()
{
    std::optional<LikedIndex> account;
    if (m_account != nullptr && m_account->isActive()) {
        account = m_account->likedIndex();
    }
    return [this, account = std::move(account)](const Track& track) {
        if (account && account->hasTrack({track.remoteId, track.title, track.artist})) {
            return true;
        }
        const QString key = catalogSourceKey(track);
        return !key.isEmpty() && m_tracks.isLiked(key);
    };
}

std::optional<bool> TrackActions::toggleLike(const Track& track)
{
    // A copy of an account song (a downloaded file, a song another device
    // sent without its account id) is liked as that song.
    std::optional<Track> accountSong;
    if (m_account != nullptr && m_account->handles(track)) {
        accountSong = track;
    } else if (m_account != nullptr && m_account->isActive() && net::isHttpUrl(track.sourceUrl)) {
        accountSong = m_account->accountCopyOf(track);
        if (!accountSong) {
            return std::nullopt; // not this account's song: nothing to like it as
        }
    }
    if (!accountSong && !canKeep(track)) {
        return std::nullopt;
    }
    const std::optional<UserDataOperationGate::Lease> lease = m_gate.tryEnter();
    if (!lease) {
        return std::nullopt;
    }
    if (accountSong) {
        const bool likedBefore = isLiked(track);
        const std::optional<bool> liked = m_account->setLiked(*accountSong, !likedBefore);
        if (liked) {
            emit likedChanged(catalogSourceKey(*accountSong), *liked);
            if (*liked) {
                emit songLiked(*accountSong);
            }
        }
        return liked;
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
    if (liked) {
        emit songLiked(track);
    }
    return liked;
}

QList<Track> TrackActions::playlists()
{
    // The account's playlists while its account is in use, then the ones
    // kept on this PC.
    const bool accountPlaylists = m_account != nullptr && m_account->isActive();
    QList<Track> mine;
    for (const Track& playlist : m_groups.loadPlaylists()) {
        if (playlist.origin != QLatin1StringView("account") || accountPlaylists) {
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
    if (account::AccountLibraryEditor::isAccountPlaylistKey(playlistKey)) {
        if (m_account == nullptr || !m_account->handles(track)) {
            return Failure::of(FailureKind::NotAvailable, QStringLiteral("only account songs go in account playlists"));
        }
        (void)m_account->addToPlaylist(playlistKey, track);
        return Result<void>::success();
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
    if (m_account != nullptr && m_account->handles(track)) {
        // Made on the service with the song in it; its key arrives later.
        (void)m_account->createPlaylist(title, {track});
        return QString();
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
    if (account::AccountLibraryEditor::isAccountPlaylistKey(playlistKey)) {
        return removeAllFromPlaylist({track}, playlistKey);
    }
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
