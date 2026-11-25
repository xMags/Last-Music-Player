#pragma once

#include "core/account/AccountDataLifecycleStorage.h"
#include "core/async/Result.h"
#include "core/db/AccountPlaylistPage.h"
#include "core/db/AccountRecords.h"
#include "core/db/Database.h"
#include "core/model/Track.h"

#include <QList>
#include <QString>

#include <cstddef>
#include <optional>

namespace lmp::core::db {

// The cached copy of each signed-in account on this PC: profiles, songs,
// playlists, the active remote context, sync state and the catalogue
// discovery cache. Every row belongs to one account id, so accounts never
// see each other's data. The queued changes are in AccountOutboxRepository,
// followed artists in FollowedArtistRepository.
//
// Not thread-safe. It works on the database's connection and belongs to the
// database's owner thread; that Database must outlive it.
//
// An operation that earlier versions reported as false returns a Failure:
// FailureKind::NotFound when there was nothing to act on (an empty id, an
// unknown mode, a missing row, an address that is not safe to keep),
// FailureKind::Storage when SQLite refused. Every SQLite time is SQLite's own
// clock in UTC: datetime('now') text, "YYYY-MM-DD HH:MM:SS".
class AccountRepository final : public AccountPlaylistTrackSource, public account::AccountDataLifecycleStorage {
public:
    explicit AccountRepository(Database& database);

    // `mode` is "LocalOnly", "ApiKey" or "Account" (exactly). The account id
    // is stored for "Account" only, which needs one and makes its profile.
    [[nodiscard]] Result<void> setRemoteLibraryContext(const QString& mode, const QString& accountId = {});
    // The account of the active context when it is "Account", else empty.
    [[nodiscard]] QString activeAccountId() const;

    // Replaces the account's profile, songs and playlists with the snapshot
    // in one transaction, and records snapshot.synchronizedAtUtc as the last
    // successful sync (keeping the history import state). Only while
    // `expectedAccountId` is the active account and the snapshot is for it.
    // Songs without a remote id, and repeats, are skipped; playlists without
    // an id, and repeats, are skipped; playlist entries name stored songs
    // only, numbered from 0. Addresses that are not safe to keep are stored
    // empty.
    [[nodiscard]] Result<void> replaceAccountLibrary(const QString& expectedAccountId,
                                                     const AccountLibrarySnapshot& snapshot);
    // Nothing when the account has no profile or a read fails. Songs newest
    // first, playlists most recently changed first. synchronizedAtUtc is the
    // last successful sync, else the newest song update.
    [[nodiscard]] std::optional<AccountLibrarySnapshot> loadAccountLibrary(const QString& accountId) const;
    // In playlist order, numbered from 1. Empty for an empty id.
    [[nodiscard]] QList<Track> loadAccountPlaylistTracks(const QString& accountId,
                                                         const QString& playlistId) const override;

    // Caches the song's metadata; see account_rows::upsertAccountTrackMetadata.
    [[nodiscard]] Result<void> ensureAccountTrack(const QString& accountId, const Track& track);
    // Caches the song and counts one play at `positionSeconds` (negative
    // counts as 0), in one transaction.
    [[nodiscard]] Result<void> recordAccountPlayback(const QString& accountId,
                                                     const Track& track,
                                                     double positionSeconds);
    // Succeeds even when the song is not cached, as before.
    [[nodiscard]] Result<void> updateAccountTrackLike(const QString& accountId,
                                                      const QString& remoteTrackId,
                                                      bool liked);

    // The stored liked flag of an account song; nothing when it is not cached.
    [[nodiscard]] std::optional<bool> isAccountTrackLiked(const QString& accountId, const QString& remoteTrackId) const;
    // The account's liked songs: remote id, title and artist only.
    [[nodiscard]] QList<Track> loadAccountLikedTracks(const QString& accountId) const;
    // The remote id of the account song with this source address, when one is cached.
    [[nodiscard]] std::optional<QString> findAccountTrackIdBySource(const QString& accountId,
                                                                    const QString& sourceUrl) const;

    // The stored state, or one holding only the account id.
    [[nodiscard]] AccountSyncState loadAccountSyncState(const QString& accountId) const;
    [[nodiscard]] Result<void> saveAccountSyncState(const AccountSyncState& state);

    // The last catalogue discovery payload for the account and region, or
    // empty, which callers treat as a cold start.
    [[nodiscard]] QString loadCatalogDiscoveryPayload(const QString& accountId, const QString& storefront) const;
    // Refuses (NotFound) an empty payload or one over 4 Mi characters.
    [[nodiscard]] Result<void> saveCatalogDiscoveryPayload(const QString& accountId,
                                                           const QString& storefront,
                                                           const QString& payload);

    // Deletes the account's profile, which takes every row of the account
    // with it, in one transaction; see AccountDataClearMode.
    [[nodiscard]] Result<void> clearAccountData(const QString& accountId,
                                                AccountDataClearMode mode = AccountDataClearMode::LeaveInactive);
    // Every account's data, and the active context goes back to local only.
    [[nodiscard]] Result<void> clearAllAccountData();

    // AccountDataLifecycleStorage. The count is AccountOutboxRepository's;
    // removal is clearAccountData with LeaveInactive.
    [[nodiscard]] bool isAccountDataStoreReady() const noexcept override;
    [[nodiscard]] std::optional<std::size_t> recoverablePendingChangeCount(const QString& accountId) const override;
    [[nodiscard]] Result<void> removeSignedOutAccountData(const QString& accountId) override;

private:
    [[nodiscard]] Result<void> writeLibrary(const QString& accountId, const AccountLibrarySnapshot& snapshot);

    Database& m_database;
    SqliteConnection& m_connection;
};

} // namespace lmp::core::db
