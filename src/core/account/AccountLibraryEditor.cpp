#include "core/account/AccountLibraryEditor.h"

#include "core/catalog/CollectionSourceKey.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "core/account/AccountClient.h"
#include "core/account/AccountLibrarySync.h"
#include "core/account/AccountMusicContract.h"
#include "core/account/AccountSessionService.h"
#include "core/account/MusicSyncService.h"
#include "core/db/AccountOutboxRepository.h"
#include "core/db/AccountRepository.h"
#include "core/library/LikedIndex.h"
#include "core/net/RemoteUrlPolicy.h"
#include "core/library/SameSong.h"
#include "core/library/UserDataOperationGate.h"
#include "core/mode/ModeService.h"
#include "core/remote/TrackJson.h"

#include <QDateTime>
#include <QUuid>

#include <utility>

namespace lmp::core::account {

namespace {

template <typename T>
[[nodiscard]] QFuture<Result<T>> refused(const QString& detail)
{
    return QtFuture::makeReadyValueFuture(Result<T>(Failure::of(FailureKind::NotAvailable, detail)));
}

[[nodiscard]] QString playlistIdOf(const QString& key)
{
    return key.mid(AccountLibraryEditor::PlaylistKeyPrefix.size());
}

[[nodiscard]] SongIdentity identityOf(const Track& track)
{
    return {track.remoteId, track.title, track.artist};
}

} // namespace

AccountLibraryEditor::AccountLibraryEditor(Dependencies dependencies, QObject* parent)
    : QObject(parent)
    , m_deps(std::move(dependencies))
{
    if (!m_deps.nowUtc) {
        m_deps.nowUtc = [] { return QDateTime::currentDateTimeUtc().toString(Qt::ISODate); };
    }
    if (!m_deps.newEventId) {
        // The earlier app's form: a braced upper-case GUID.
        // A bare GUID: the account service refuses any other form (braces
        // included) as an invalid history event.
        m_deps.newEventId = [] { return QUuid::createUuid().toString(QUuid::WithoutBraces).toUpper(); };
    }
}

bool AccountLibraryEditor::isActive() const
{
    return writableContext().has_value();
}

std::optional<AccountOperationContext> AccountLibraryEditor::writableContext() const
{
    if (!m_deps.mode.accountSelected()) {
        return std::nullopt;
    }
    std::optional<AccountOperationContext> context = m_deps.session.captureOperation();
    if (!context || context->ownerId.isEmpty()
        || (context->state != AccountSessionState::Validated && context->state != AccountSessionState::Offline)) {
        return std::nullopt;
    }
    return context;
}

bool AccountLibraryEditor::handles(const Track& track) const
{
    return isActive() && remote::track_json::isCompatibleAccountRemoteTrack(track);
}

bool AccountLibraryEditor::isAccountPlaylistKey(const QString& key)
{
    return key.startsWith(PlaylistKeyPrefix) && key.size() > PlaylistKeyPrefix.size();
}

bool AccountLibraryEditor::isLiked(const Track& track) const
{
    const std::optional<AccountOperationContext> context = writableContext();
    if (!context) {
        return false;
    }
    return m_deps.accounts.isAccountTrackLiked(context->ownerId, track.remoteId).value_or(track.liked);
}

std::optional<LikedIndex> AccountLibraryEditor::likedIndex() const
{
    const std::optional<AccountOperationContext> context = writableContext();
    if (!context) {
        return std::nullopt;
    }
    QList<SongIdentity> liked;
    for (const Track& stored : m_deps.accounts.loadAccountLikedTracks(context->ownerId)) {
        liked.append(identityOf(stored));
    }
    return LikedIndex(liked);
}

std::optional<Track> AccountLibraryEditor::accountCopyOf(const Track& track) const
{
    const std::optional<AccountOperationContext> context = writableContext();
    if (!context || !net::isSafeRemoteUrl(track.sourceUrl, net::RemoteUrlUse::Durable)) {
        return std::nullopt;
    }
    QString remoteId = track.remoteId;
    if (remoteId.isEmpty()) {
        QList<SongIdentity> liked;
        for (const Track& stored : m_deps.accounts.loadAccountLikedTracks(context->ownerId)) {
            liked.append(identityOf(stored));
        }
        if (const std::optional<SongIdentity> copy = LikedIndex(liked).copyOf(identityOf(track))) {
            remoteId = copy->id;
        }
    }
    if (remoteId.isEmpty()) {
        remoteId = m_deps.accounts.findAccountTrackIdBySource(context->ownerId, track.sourceUrl).value_or(QString());
    }
    if (remoteId.isEmpty()) {
        return std::nullopt;
    }
    Track copy = track;
    copy.remoteId = remoteId;
    copy.sourceKind = QStringLiteral("remote");
    copy.filePath.clear();
    if (!remote::track_json::isCompatibleAccountRemoteTrack(copy)) {
        return std::nullopt;
    }
    return copy;
}

bool AccountLibraryEditor::writeLike(const QString& owner, const Track& track, bool liked)
{
    const std::optional<bool> before = m_deps.accounts.isAccountTrackLiked(owner, track.remoteId);
    // The stored song first, so every view shows the change at once.
    if (!m_deps.accounts.ensureAccountTrack(owner, track)
        || !m_deps.accounts.updateAccountTrackLike(owner, track.remoteId, liked)) {
        return false;
    }
    db::PendingLikeRecord record;
    record.accountId = owner;
    record.remoteTrackId = track.remoteId;
    record.track = track;
    record.desiredState = liked;
    record.updatedAtUtc = m_deps.nowUtc();
    if (!m_deps.outbox.enqueuePendingLike(record)) {
        // Put back as it was: the change could not be queued for the service.
        (void)m_deps.accounts.updateAccountTrackLike(owner, track.remoteId, before.value_or(!liked));
        return false;
    }
    return true;
}

std::optional<bool> AccountLibraryEditor::setLiked(const Track& track, bool liked)
{
    const std::optional<AccountOperationContext> context = writableContext();
    if (!context || !remote::track_json::isCompatibleAccountRemoteTrack(track)) {
        return std::nullopt;
    }
    const std::optional<UserDataOperationGate::Lease> lease = m_deps.gate.tryEnter();
    if (!lease) {
        return std::nullopt;
    }
    if (!writeLike(context->ownerId, track, liked)) {
        emit failed(tr("Could not save the like. Try again."));
        return std::nullopt;
    }
    if (!liked) {
        // Every other liked version of the same song goes too.
        QList<SongIdentity> likedSongs;
        QHash<QString, Track> byId;
        for (const Track& stored : m_deps.accounts.loadAccountLikedTracks(context->ownerId)) {
            likedSongs.append(identityOf(stored));
            byId.insert(stored.remoteId, stored);
        }
        const LikedIndex index(likedSongs);
        for (const SongIdentity& copy : index.copiesOf(identityOf(track))) {
            if (copy.id == track.remoteId || !byId.contains(copy.id)) {
                continue;
            }
            // An unlike sends only the copy's id; the queued song keeps this
            // song's address so the queue accepts it as a valid song.
            Track other = track;
            other.remoteId = copy.id;
            other.title = copy.title;
            other.artist = copy.artist;
            (void)m_deps.accounts.updateAccountTrackLike(context->ownerId, copy.id, false);
            db::PendingLikeRecord record;
            record.accountId = context->ownerId;
            record.remoteTrackId = copy.id;
            record.track = other;
            record.desiredState = false;
            record.updatedAtUtc = m_deps.nowUtc();
            (void)m_deps.outbox.enqueuePendingLike(record);
        }
    }
    m_deps.sync.scheduleMutation(MusicSyncService::Lane::Likes);
    return liked;
}

QFuture<Result<void>> AccountLibraryEditor::afterEdit(QFuture<Result<void>> call, const AccountOperationContext& context,
                                                      const QString& playlistKey, const QString& failureMessage)
{
    return call.then(this, [this, context, playlistKey, failureMessage](const Result<void>& result) -> Result<void> {
        if (!m_deps.session.isCurrent(context)) {
            return Failure::of(FailureKind::Cancelled, QStringLiteral("account changed"));
        }
        if (!result) {
            if (result.failure().kind == FailureKind::Unauthorized) {
                m_deps.session.handleUnauthorized(context);
            }
            emit failed(failureMessage);
            return result;
        }
        // The service holds the change; the stored copy follows.
        m_deps.sync.requestSync(MusicSyncService::Lane::Library).then(this, [this, playlistKey](bool) {
            if (playlistKey.isEmpty()) {
                emit playlistsChanged();
            } else {
                emit playlistChanged(playlistKey);
                emit playlistsChanged();
            }
        });
        return result;
    });
}

QFuture<Result<QString>> AccountLibraryEditor::createPlaylist(const QString& name, const QList<Track>& tracks)
{
    const std::optional<AccountOperationContext> context = writableContext();
    const QString title = name.trimmed();
    if (!context || title.isEmpty()) {
        return refused<QString>(QStringLiteral("not in Account mode"));
    }
    // The reference client sends the first songs with the new playlist.
    QList<PortableAccountTrack> portable;
    for (const Track& track : tracks) {
        if (remote::track_json::isCompatibleAccountRemoteTrack(track)) {
            portable.append(portableFromTrack(track));
        }
    }
    const QByteArray body = portable.isEmpty() ? remote::track_json::accountPlaylistJson(title)
                                               : serializePlaylistReplacement(title, portable);
    return m_deps.client.createPlaylist(context->bearerSession, body)
        .then(this, [this, context = *context](const Result<AccountPlaylist>& created) -> Result<QString> {
            if (!m_deps.session.isCurrent(context)) {
                return Failure::of(FailureKind::Cancelled, QStringLiteral("account changed"));
            }
            if (!created) {
                if (created.failure().kind == FailureKind::Unauthorized) {
                    m_deps.session.handleUnauthorized(context);
                }
                emit failed(tr("Could not create the playlist."));
                return created.failure();
            }
            const QString key = QString(PlaylistKeyPrefix) + created.value().id;
            m_deps.sync.requestSync(MusicSyncService::Lane::Library).then(this, [this](bool) {
                emit playlistsChanged();
            });
            return key;
        });
}

QFuture<Result<void>> AccountLibraryEditor::renamePlaylist(const QString& playlistKey, const QString& name)
{
    const std::optional<AccountOperationContext> context = writableContext();
    const QByteArray body = remote::track_json::accountPlaylistJson(name);
    if (!context || !isAccountPlaylistKey(playlistKey) || body.isEmpty()) {
        return refused<void>(QStringLiteral("not an account playlist"));
    }
    return afterEdit(m_deps.client.updatePlaylist(context->bearerSession, playlistIdOf(playlistKey), body), *context,
                     playlistKey, tr("Could not rename the playlist."));
}

QFuture<Result<QString>> AccountLibraryEditor::saveCollection(const QString& title, const QString& description,
                                                              const QList<Track>& tracks, const QString& sourceUrl)
{
    const std::optional<AccountOperationContext> context = writableContext();
    const QString name = title.trimmed();
    if (!context || name.isEmpty() || catalog::canonicalSourceKey(sourceUrl).isEmpty()) {
        return refused<QString>(QStringLiteral("this collection cannot be saved"));
    }
    QJsonArray songs;
    for (const Track& track : tracks) {
        if (!remote::track_json::isCompatibleAccountRemoteTrack(track)) {
            continue;
        }
        const QJsonDocument song = QJsonDocument::fromJson(serializePortableTrack(portableFromTrack(track)));
        if (song.isObject()) {
            songs.append(song.object());
        }
    }
    const QJsonObject body{{QStringLiteral("name"), name},
                           {QStringLiteral("description"), description.trimmed()},
                           {QStringLiteral("tracks"), songs},
                           {QStringLiteral("sourceUrl"), sourceUrl.trimmed()}};
    return m_deps.client.createPlaylist(context->bearerSession, QJsonDocument(body).toJson(QJsonDocument::Compact))
        .then(this, [this, context = *context](const Result<AccountPlaylist>& created) -> Result<QString> {
            if (!m_deps.session.isCurrent(context)) {
                return Failure::of(FailureKind::Cancelled, QStringLiteral("account changed"));
            }
            if (!created) {
                if (created.failure().kind == FailureKind::Unauthorized) {
                    m_deps.session.handleUnauthorized(context);
                }
                emit failed(tr("Could not save to your library."));
                return created.failure();
            }
            m_deps.sync.requestSync(MusicSyncService::Lane::Library).then(this, [this](bool) {
                emit playlistsChanged();
            });
            return QString(PlaylistKeyPrefix) + created.value().id;
        });
}

std::optional<QString> AccountLibraryEditor::savedCollectionKey(const QString& sourceUrl) const
{
    const QString wanted = catalog::canonicalSourceKey(sourceUrl);
    const QString owner = m_deps.session.profile().id;
    if (wanted.isEmpty() || owner.isEmpty()) {
        return std::nullopt;
    }
    const std::optional<db::AccountLibrarySnapshot> library = m_deps.accounts.loadAccountLibrary(owner);
    if (!library) {
        return std::nullopt;
    }
    for (const db::AccountPlaylistSnapshot& playlist : library->playlists) {
        if (catalog::canonicalSourceKey(playlist.sourceUrl) == wanted) {
            return QString(PlaylistKeyPrefix) + playlist.playlistId;
        }
    }
    return std::nullopt;
}

QFuture<Result<void>> AccountLibraryEditor::removeSavedCollection(const QString& sourceUrl)
{
    const std::optional<QString> key = savedCollectionKey(sourceUrl);
    if (!key) {
        return refused<void>(QStringLiteral("no saved copy"));
    }
    return deletePlaylist(*key);
}

QFuture<Result<void>> AccountLibraryEditor::deletePlaylist(const QString& playlistKey)
{
    const std::optional<AccountOperationContext> context = writableContext();
    if (!context || !isAccountPlaylistKey(playlistKey)) {
        return refused<void>(QStringLiteral("not an account playlist"));
    }
    return afterEdit(m_deps.client.deletePlaylist(context->bearerSession, playlistIdOf(playlistKey)), *context, {},
                     tr("Could not delete the playlist."));
}

QFuture<Result<void>> AccountLibraryEditor::addToPlaylist(const QString& playlistKey, const Track& track)
{
    const std::optional<AccountOperationContext> context = writableContext();
    const QByteArray trackJson = remote::track_json::accountTrackJson(track);
    if (!context || !isAccountPlaylistKey(playlistKey) || trackJson.isEmpty()) {
        return refused<void>(QStringLiteral("not an account playlist or song"));
    }
    return afterEdit(m_deps.client.addPlaylistTrack(context->bearerSession, playlistIdOf(playlistKey), trackJson),
                     *context, playlistKey, tr("Could not add the song to the playlist."));
}

QFuture<Result<void>> AccountLibraryEditor::removeFromPlaylist(const QString& playlistKey, const QList<Track>& tracks)
{
    const std::optional<AccountOperationContext> context = writableContext();
    if (!context || !isAccountPlaylistKey(playlistKey) || tracks.isEmpty()) {
        return refused<void>(QStringLiteral("not an account playlist"));
    }
    // One call per song, one after the other: the service removes only the
    // songs named, so songs another device added meanwhile stay.
    QFuture<Result<void>> chain = QtFuture::makeReadyValueFuture(Result<void>::success());
    const QString bearer = context->bearerSession;
    const QString playlistId = playlistIdOf(playlistKey);
    for (const Track& track : tracks) {
        const QString trackId = track.remoteId;
        chain = chain
                    .then(this,
                          [this, bearer, playlistId, trackId](const Result<void>& previous) -> QFuture<Result<void>> {
                              if (!previous) {
                                  return QtFuture::makeReadyValueFuture(previous);
                              }
                              return m_deps.client.removePlaylistTrack(bearer, playlistId, trackId);
                          })
                    .unwrap();
    }
    return afterEdit(chain, *context, playlistKey, tr("Could not remove the song from the playlist."));
}

QFuture<Result<void>> AccountLibraryEditor::replacePlaylist(const QString& playlistKey, const QList<Track>& tracks)
{
    const std::optional<AccountOperationContext> context = writableContext();
    if (!context || !isAccountPlaylistKey(playlistKey)) {
        return refused<void>(QStringLiteral("not an account playlist"));
    }
    return afterEdit(m_deps.client.updatePlaylist(context->bearerSession, playlistIdOf(playlistKey),
                                                  remote::track_json::accountPlaylistUpdateJson(QString(), tracks)),
                     *context, playlistKey, tr("Could not save the playlist order."));
}

bool AccountLibraryEditor::recordAccountPlay(const Track& track, double positionSeconds)
{
    const std::optional<AccountOperationContext> context = writableContext();
    if (!context) {
        return false;
    }
    // A downloaded copy of an account song plays as that song: counted once,
    // synced, and in the account's history with its other plays.
    const std::optional<Track> song =
        remote::track_json::isCompatibleAccountRemoteTrack(track) ? std::optional<Track>(track) : accountCopyOf(track);
    if (!song) {
        return false;
    }
    const std::optional<UserDataOperationGate::Lease> lease = m_deps.gate.tryEnter();
    if (!lease) {
        return true; // the play belongs to the account; nothing may be written now
    }
    db::PlaybackEventRecord event;
    event.eventId = m_deps.newEventId();
    event.accountId = context->ownerId;
    event.remoteTrackId = song->remoteId;
    event.track = *song;
    event.playedAtUtc = m_deps.nowUtc();
    event.positionSeconds = std::max(0.0, positionSeconds);
    if (!m_deps.outbox.enqueuePlaybackEvent(event)) {
        return true;
    }
    (void)m_deps.accounts.recordAccountPlayback(context->ownerId, *song, positionSeconds);
    if (context->state == AccountSessionState::Validated) {
        m_deps.sync.scheduleMutation(MusicSyncService::Lane::History);
    }
    return true;
}

} // namespace lmp::core::account
