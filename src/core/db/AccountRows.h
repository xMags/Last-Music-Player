#pragma once

#include "core/async/Result.h"
#include "core/db/SqliteConnection.h"
#include "core/model/Track.h"

#include <QString>

// Pieces the account repositories share: cleaning addresses before they are
// stored, reading an AccountTracks row, and the writes several operations
// start with. The SQL text is copied character for character from earlier
// versions.
namespace lmp::core::db::account_rows {

// A durable address with any obsolete server credential removed, or empty
// when it is not safe to keep.
[[nodiscard]] QString sanitizeAccountUrl(const QString& value);
// durableAccountArtworkUrl of the address with any obsolete credential
// removed: an account cover link, or a durable artwork address.
[[nodiscard]] QString sanitizeAccountArtworkUrl(const QString& value);
// A safe inline picture is kept verbatim (it has no origin to check and lets
// the cached account keep a picture offline); anything else is an address.
[[nodiscard]] QString sanitizeAccountAvatar(const QString& value);

// The columns rowid, RemoteId, SourceUrl, Title, Artist, Album, Genre,
// DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText,
// DurationText, IsLiked, RemoteProvider, RemoteCatalogId, AlbumCatalogId,
// ArtistCatalogId, starting at `firstColumn`. The row id is negated, as the
// EffectiveTracks view does, so it never collides with a Tracks id.
[[nodiscard]] Track accountTrackFromStatement(const SqliteStatement& statement, int firstColumn = 0);

// Makes an empty profile row for the account unless it has one.
[[nodiscard]] Result<void> ensureAccountProfile(SqliteConnection& connection, const QString& accountId);

// Inserts or refreshes the song's cached metadata; empty values keep what is
// stored. Needs an account, a remote id and a source address that is safe to
// keep (NotFound otherwise).
[[nodiscard]] Result<void> upsertAccountTrackMetadata(SqliteConnection& connection,
                                                      const QString& accountId,
                                                      const Track& track);

// COMMIT, or ROLLBACK and the failure when the commit fails.
[[nodiscard]] Result<void> commitOrRollback(SqliteConnection& connection);

// The value sqlite3_column_int would give: the low 32 bits of the integer.
[[nodiscard]] int columnInt(const SqliteStatement& statement, int column);

} // namespace lmp::core::db::account_rows
