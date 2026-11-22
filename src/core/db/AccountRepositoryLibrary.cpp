#include "core/db/AccountRepository.h"

#include "core/db/AccountRows.h"
#include "core/db/SqlTransaction.h"

#include <QByteArray>
#include <QSet>

#include <algorithm>
#include <initializer_list>
#include <utility>

// The cached account library: replacing and loading it, account playlist
// songs, and the per-song writes made while listening.
namespace lmp::core::db {

namespace {

namespace tx = sql_transaction;

constexpr char kActiveAccountSql[] =
    "SELECT AccountId FROM ActiveAccountContext WHERE SingletonId=1 AND RemoteMode='Account';";

constexpr char kUpsertProfileSql[] =
    "INSERT INTO AccountProfiles (AccountId, DisplayName, Username, AvatarUrl, PlanLabel, UpdatedAtUtc) "
    "VALUES (?, ?, ?, ?, ?, ?) ON CONFLICT(AccountId) DO UPDATE SET "
    "DisplayName=excluded.DisplayName, Username=excluded.Username, AvatarUrl=excluded.AvatarUrl, "
    "PlanLabel=excluded.PlanLabel, UpdatedAtUtc=excluded.UpdatedAtUtc;";

constexpr char kInsertTrackSql[] =
    "INSERT INTO AccountTracks (AccountId, RemoteId, SourceUrl, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, "
    "DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteProvider, RemoteCatalogId, AlbumCatalogId, ArtistCatalogId, "
    "PlayCount, FirstPlayedAtUtc, LastPlayedAtUtc, LastPositionSeconds, UpdatedAtUtc) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";

constexpr char kInsertPlaylistSql[] =
    "INSERT INTO AccountPlaylists (AccountId, PlaylistId, Name, Description, SourceUrl, UpdatedAtUtc) "
    "VALUES (?, ?, ?, ?, ?, ?);";

constexpr char kInsertPlaylistTrackSql[] =
    "INSERT OR IGNORE INTO AccountPlaylistTracks (AccountId, PlaylistId, RemoteId, TrackOrder) VALUES (?, ?, ?, ?);";

constexpr char kLoadProfileSql[] =
    "SELECT AccountId, DisplayName, Username, AvatarUrl, PlanLabel, UpdatedAtUtc FROM AccountProfiles WHERE AccountId=?;";

constexpr char kLoadTracksSql[] =
    "SELECT rowid, RemoteId, SourceUrl, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, "
    "DateAddedText, DurationText, IsLiked, RemoteProvider, RemoteCatalogId, AlbumCatalogId, ArtistCatalogId, "
    "PlayCount, FirstPlayedAtUtc, LastPlayedAtUtc, LastPositionSeconds, UpdatedAtUtc "
    "FROM AccountTracks WHERE AccountId=? ORDER BY DateAddedSortKey DESC, Title COLLATE NOCASE ASC;";

constexpr char kLoadPlaylistsSql[] =
    "SELECT PlaylistId, Name, Description, SourceUrl, UpdatedAtUtc FROM AccountPlaylists WHERE AccountId=? "
    "ORDER BY UpdatedAtUtc DESC, Name COLLATE NOCASE ASC;";

constexpr char kLoadPlaylistIdsSql[] =
    "SELECT RemoteId FROM AccountPlaylistTracks WHERE AccountId=? AND PlaylistId=? ORDER BY TrackOrder;";

constexpr char kLoadPlaylistTracksSql[] =
    "SELECT t.rowid, t.RemoteId, t.SourceUrl, t.Title, t.Artist, t.Album, t.Genre, t.DurationSeconds, t.ArtworkUrl, "
    "t.DateAddedSortKey, t.DateAddedText, t.DurationText, t.IsLiked, t.RemoteProvider, t.RemoteCatalogId, "
    "t.AlbumCatalogId, t.ArtistCatalogId "
    "FROM AccountPlaylistTracks pt JOIN AccountTracks t ON t.AccountId=pt.AccountId AND t.RemoteId=pt.RemoteId "
    "WHERE pt.AccountId=? AND pt.PlaylistId=? ORDER BY pt.TrackOrder, t.Title COLLATE NOCASE;";

constexpr char kRecordPlaybackSql[] =
    "UPDATE AccountTracks SET "
    "PlayCount=COALESCE(PlayCount,0)+1, "
    "FirstPlayedAtUtc=COALESCE(NULLIF(FirstPlayedAtUtc,''),datetime('now')), "
    "LastPlayedAtUtc=datetime('now'), LastPositionSeconds=?, UpdatedAtUtc=datetime('now') "
    "WHERE AccountId=? AND RemoteId=?;";

void bindTrackRow(SqliteStatement& statement,
                  const QString& accountId,
                  const AccountTrackSnapshot& entry,
                  const QString& synchronizedAtUtc)
{
    const Track& track = entry.track;
    statement.bindText(1, accountId);
    statement.bindText(2, track.remoteId);
    statement.bindText(3, account_rows::sanitizeAccountUrl(track.sourceUrl));
    statement.bindText(4, track.title);
    statement.bindText(5, track.artist);
    statement.bindText(6, track.album);
    statement.bindText(7, track.genre);
    statement.bindDouble(8, track.durationSeconds);
    statement.bindText(9, account_rows::sanitizeAccountArtworkUrl(track.artworkUrl));
    statement.bindDouble(10, track.dateAddedSortKey);
    statement.bindText(11, track.dateAdded);
    statement.bindText(12, track.durationText);
    statement.bindInt64(13, track.liked ? 1 : 0);
    statement.bindText(14, track.sourceTag);
    statement.bindText(15, track.catalogId);
    statement.bindText(16, track.albumCatalogId);
    statement.bindText(17, track.artistCatalogId);
    statement.bindInt64(18, entry.playCount);
    statement.bindText(19, entry.firstPlayedAtUtc);
    statement.bindText(20, entry.lastPlayedAtUtc);
    statement.bindDouble(21, entry.lastPositionSeconds);
    statement.bindText(22, synchronizedAtUtc);
}

// Runs a prepared statement again with fresh bindings.
void rearm(SqliteStatement& statement)
{
    statement.reset();
    statement.clearBindings();
}

} // namespace

Result<void> AccountRepository::replaceAccountLibrary(const QString& expectedAccountId,
                                                      const AccountLibrarySnapshot& snapshot)
{
    const QString& accountId = snapshot.profile.accountId;
    if (expectedAccountId.isEmpty() || accountId != expectedAccountId) {
        return tx::notFound(QStringLiteral("snapshot is not for the expected account"));
    }

    {
        auto active = m_connection.prepare(kActiveAccountSql);
        if (!active) {
            return active.failure();
        }
        const Result<bool> found = active.value().step();
        if (!found) {
            return found.failure();
        }
        if (!found.value() || active.value().columnText(0) != expectedAccountId) {
            return tx::notFound(QStringLiteral("account is not the active context"));
        }
    }

    if (const Result<void> begun = m_connection.exec("BEGIN IMMEDIATE;"); !begun) {
        return begun;
    }
    if (const Result<void> written = writeLibrary(accountId, snapshot); !written) {
        return tx::rollbackWith(m_connection, written.failure());
    }
    return account_rows::commitOrRollback(m_connection);
}

Result<void> AccountRepository::writeLibrary(const QString& accountId, const AccountLibrarySnapshot& snapshot)
{
    auto profile = m_connection.prepare(kUpsertProfileSql);
    if (!profile) {
        return profile.failure();
    }
    profile.value().bindText(1, accountId);
    profile.value().bindText(2, snapshot.profile.displayName);
    profile.value().bindText(3, snapshot.profile.username);
    profile.value().bindText(4, account_rows::sanitizeAccountAvatar(snapshot.profile.avatarUrl));
    profile.value().bindText(5, snapshot.profile.planLabel);
    profile.value().bindText(6, snapshot.profile.updatedAtUtc);
    if (const Result<void> ran = profile.value().run(); !ran) {
        return ran;
    }

    for (const char* table : {"AccountPlaylistTracks", "AccountPlaylists", "AccountTracks"}) {
        const QByteArray sql = QByteArray("DELETE FROM ") + table + " WHERE AccountId=?;";
        auto remove = m_connection.prepare(sql);
        if (!remove) {
            return remove.failure();
        }
        remove.value().bindText(1, accountId);
        if (const Result<void> ran = remove.value().run(); !ran) {
            return ran;
        }
    }

    auto insertTrack = m_connection.prepare(kInsertTrackSql);
    if (!insertTrack) {
        return insertTrack.failure();
    }
    QSet<QString> insertedTrackIds;
    for (const AccountTrackSnapshot& entry : snapshot.tracks) {
        const QString& remoteId = entry.track.remoteId;
        if (remoteId.isEmpty() || insertedTrackIds.contains(remoteId)) {
            continue;
        }
        insertedTrackIds.insert(remoteId);
        rearm(insertTrack.value());
        bindTrackRow(insertTrack.value(), accountId, entry, snapshot.synchronizedAtUtc);
        if (const Result<void> ran = insertTrack.value().run(); !ran) {
            return ran;
        }
    }

    auto insertPlaylist = m_connection.prepare(kInsertPlaylistSql);
    auto insertPlaylistTrack = m_connection.prepare(kInsertPlaylistTrackSql);
    if (!insertPlaylist) {
        return insertPlaylist.failure();
    }
    if (!insertPlaylistTrack) {
        return insertPlaylistTrack.failure();
    }
    QSet<QString> insertedPlaylistIds;
    for (const AccountPlaylistSnapshot& playlist : snapshot.playlists) {
        if (playlist.playlistId.isEmpty() || insertedPlaylistIds.contains(playlist.playlistId)) {
            continue;
        }
        insertedPlaylistIds.insert(playlist.playlistId);
        SqliteStatement& playlistRow = insertPlaylist.value();
        rearm(playlistRow);
        playlistRow.bindText(1, accountId);
        playlistRow.bindText(2, playlist.playlistId);
        playlistRow.bindText(3, playlist.name);
        playlistRow.bindText(4, playlist.description);
        playlistRow.bindText(5, account_rows::sanitizeAccountUrl(playlist.sourceUrl));
        playlistRow.bindText(6, playlist.updatedAtUtc);
        if (const Result<void> ran = playlistRow.run(); !ran) {
            return ran;
        }

        qint64 order = 0;
        for (const QString& remoteId : playlist.remoteTrackIds) {
            if (!insertedTrackIds.contains(remoteId)) {
                continue;
            }
            SqliteStatement& member = insertPlaylistTrack.value();
            rearm(member);
            member.bindText(1, accountId);
            member.bindText(2, playlist.playlistId);
            member.bindText(3, remoteId);
            member.bindInt64(4, order++);
            if (const Result<void> ran = member.run(); !ran) {
                return ran;
            }
        }
    }

    // The import decision outlives a sync; the last error does not.
    AccountSyncState state;
    state.accountId = accountId;
    state.lastSuccessfulSyncUtc = snapshot.synchronizedAtUtc;
    state.legacyHistoryImportState = loadAccountSyncState(accountId).legacyHistoryImportState;
    return saveAccountSyncState(state);
}

std::optional<AccountLibrarySnapshot> AccountRepository::loadAccountLibrary(const QString& accountId) const
{
    if (accountId.isEmpty()) {
        return std::nullopt;
    }

    AccountLibrarySnapshot snapshot;
    auto profile = m_connection.prepare(kLoadProfileSql);
    if (!profile) {
        return std::nullopt;
    }
    profile.value().bindText(1, accountId);
    if (!profile.value().step().valueOr(false)) {
        return std::nullopt;
    }
    snapshot.profile.accountId = profile.value().columnText(0);
    snapshot.profile.displayName = profile.value().columnText(1);
    snapshot.profile.username = profile.value().columnText(2);
    snapshot.profile.avatarUrl = profile.value().columnText(3);
    snapshot.profile.planLabel = profile.value().columnText(4);
    snapshot.profile.updatedAtUtc = profile.value().columnText(5);

    auto tracks = m_connection.prepare(kLoadTracksSql);
    if (!tracks) {
        return std::nullopt;
    }
    tracks.value().bindText(1, accountId);
    while (tracks.value().step().valueOr(false)) {
        const SqliteStatement& row = tracks.value();
        AccountTrackSnapshot entry;
        entry.track = account_rows::accountTrackFromStatement(row);
        entry.playCount = row.columnInt64(17);
        entry.firstPlayedAtUtc = row.columnText(18);
        entry.lastPlayedAtUtc = row.columnText(19);
        entry.lastPositionSeconds = row.columnDouble(20);
        // The newest update, compared as text: the stored times sort that way.
        if (QString updatedAtUtc = row.columnText(21); updatedAtUtc > snapshot.synchronizedAtUtc) {
            snapshot.synchronizedAtUtc = std::move(updatedAtUtc);
        }
        snapshot.tracks.append(std::move(entry));
    }

    auto playlists = m_connection.prepare(kLoadPlaylistsSql);
    if (!playlists) {
        return std::nullopt;
    }
    playlists.value().bindText(1, accountId);
    while (playlists.value().step().valueOr(false)) {
        const SqliteStatement& row = playlists.value();
        AccountPlaylistSnapshot playlist;
        playlist.playlistId = row.columnText(0);
        playlist.name = row.columnText(1);
        playlist.description = row.columnText(2);
        playlist.sourceUrl = row.columnText(3);
        playlist.updatedAtUtc = row.columnText(4);

        auto ids = m_connection.prepare(kLoadPlaylistIdsSql);
        if (!ids) {
            return std::nullopt;
        }
        ids.value().bindText(1, accountId);
        ids.value().bindText(2, playlist.playlistId);
        while (ids.value().step().valueOr(false)) {
            playlist.remoteTrackIds.append(ids.value().columnText(0));
        }
        snapshot.playlists.append(std::move(playlist));
    }

    const AccountSyncState state = loadAccountSyncState(accountId);
    if (!state.lastSuccessfulSyncUtc.isEmpty()) {
        snapshot.synchronizedAtUtc = state.lastSuccessfulSyncUtc;
    }
    return snapshot;
}

QList<Track> AccountRepository::loadAccountPlaylistTracks(const QString& accountId, const QString& playlistId) const
{
    QList<Track> result;
    if (accountId.isEmpty() || playlistId.isEmpty()) {
        return result;
    }
    auto prepared = m_connection.prepare(kLoadPlaylistTracksSql);
    if (!prepared) {
        return result;
    }
    prepared.value().bindText(1, accountId);
    prepared.value().bindText(2, playlistId);
    int index = 1;
    while (prepared.value().step().valueOr(false)) {
        Track track = account_rows::accountTrackFromStatement(prepared.value());
        track.index = index++;
        result.append(std::move(track));
    }
    return result;
}

Result<void> AccountRepository::ensureAccountTrack(const QString& accountId, const Track& track)
{
    return account_rows::upsertAccountTrackMetadata(m_connection, accountId, track);
}

Result<void> AccountRepository::recordAccountPlayback(const QString& accountId,
                                                      const Track& track,
                                                      double positionSeconds)
{
    if (accountId.isEmpty() || track.remoteId.isEmpty()) {
        return tx::notFound(QStringLiteral("empty account or track id"));
    }
    if (const Result<void> begun = m_connection.exec("BEGIN IMMEDIATE;"); !begun) {
        return begun;
    }
    if (const Result<void> cached = account_rows::upsertAccountTrackMetadata(m_connection, accountId, track); !cached) {
        return tx::rollbackWith(m_connection, cached.failure());
    }

    auto prepared = m_connection.prepare(kRecordPlaybackSql);
    if (!prepared) {
        return tx::rollbackWith(m_connection, prepared.failure());
    }
    prepared.value().bindDouble(1, std::max(0.0, positionSeconds));
    prepared.value().bindText(2, accountId);
    prepared.value().bindText(3, track.remoteId);
    if (const Result<void> ran = prepared.value().run(); !ran) {
        return tx::rollbackWith(m_connection, ran.failure());
    }
    if (m_connection.changes() != 1) {
        return tx::rollbackWith(m_connection, tx::notFound(QStringLiteral("no cached song was updated")));
    }
    return account_rows::commitOrRollback(m_connection);
}

} // namespace lmp::core::db
