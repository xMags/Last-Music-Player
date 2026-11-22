#pragma once

#include "core/async/Result.h"
#include "core/db/AccountRecords.h"
#include "core/db/Database.h"

#include <QList>
#include <QString>

namespace lmp::core::db {

// The artists the listener follows, one list per scope (an account, or an
// API key server). GroupQueries::loadArtists merges them into the artist
// cards.
//
// Not thread-safe. It works on the database's connection and belongs to the
// database's owner thread; that Database must outlive it.
//
// A refusal that earlier versions reported as false is FailureKind::NotFound:
// an empty scope or name, a value over its length limit (scope 160, name and
// catalogue id 256, provider 80, address 2048, identity key 2400 UTF-16
// units), an address that is not safe to keep, or an identity key that does
// not match the artist. FailureKind::Storage when SQLite refused.
class FollowedArtistRepository {
public:
    explicit FollowedArtistRepository(Database& database);

    // Adds the artist, or refreshes its catalogue id, address, name and
    // provider (the time it was first followed is kept). The identity key is
    // computed from the artist; a given one must equal it. An empty
    // addedAtUtc means now, "YYYY-MM-DDTHH:MM:SSZ" on SQLite's clock.
    [[nodiscard]] Result<void> followArtist(const FollowedArtist& artist);
    // Succeeds even when the artist was not followed, as before.
    [[nodiscard]] Result<void> unfollowArtist(const QString& scopeKey, const QString& identityKey);
    // False for refused input as well as for an artist not followed.
    [[nodiscard]] bool isArtistFollowed(const QString& scopeKey, const QString& identityKey) const;
    // Most recently followed first, then by name.
    [[nodiscard]] QList<FollowedArtist> loadFollowedArtists(const QString& scopeKey) const;

private:
    SqliteConnection& m_connection;
};

} // namespace lmp::core::db
