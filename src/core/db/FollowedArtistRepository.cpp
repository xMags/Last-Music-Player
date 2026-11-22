#include "core/db/FollowedArtistRepository.h"

#include "core/db/SqlTransaction.h"
#include "core/model/ArtistIdentity.h"
#include "core/net/RemoteUrlPolicy.h"

#include <utility>

namespace lmp::core::db {

namespace {

namespace tx = sql_transaction;

// Length limits in UTF-16 units, as earlier versions counted them.
constexpr qsizetype kMaxScopeKey = 160;
constexpr qsizetype kMaxName = 256;
constexpr qsizetype kMaxCatalogId = 256;
constexpr qsizetype kMaxProvider = 80;
constexpr qsizetype kMaxSourceUrl = 2048;
constexpr qsizetype kMaxIdentityKey = 2400;

bool isValidLookup(const QString& scopeKey, const QString& identityKey)
{
    return !scopeKey.isEmpty() && !identityKey.isEmpty() && scopeKey.size() <= kMaxScopeKey
        && identityKey.size() <= kMaxIdentityKey;
}

} // namespace

FollowedArtistRepository::FollowedArtistRepository(Database& database)
    : m_connection(database.connection())
{
}

Result<void> FollowedArtistRepository::followArtist(const FollowedArtist& artist)
{
    if (artist.scopeKey.isEmpty() || artist.name.isEmpty() || artist.scopeKey.size() > kMaxScopeKey
        || artist.name.size() > kMaxName || artist.catalogId.size() > kMaxCatalogId
        || artist.provider.size() > kMaxProvider || artist.sourceUrl.size() > kMaxSourceUrl) {
        return tx::notFound(QStringLiteral("followed artist is empty or too long"));
    }
    if (!artist.sourceUrl.isEmpty() && !net::isSafeRemoteUrl(artist.sourceUrl, net::RemoteUrlUse::Durable)) {
        return tx::notFound(QStringLiteral("followed artist address is not safe to keep"));
    }
    const QString identityKey =
        artistIdentityKey(ArtistIdentity{artist.catalogId, artist.sourceUrl, artist.name, artist.provider});
    if (!artist.identityKey.isEmpty() && artist.identityKey != identityKey) {
        return tx::notFound(QStringLiteral("identity key does not match the artist"));
    }
    if (identityKey.isEmpty() || identityKey.size() > kMaxIdentityKey) {
        return tx::notFound(QStringLiteral("followed artist has no usable identity"));
    }

    auto prepared = m_connection.prepare(
        "INSERT INTO FollowedArtists "
        "(ScopeKey, IdentityKey, CatalogId, SourceUrl, Name, Provider, AddedAtUtc) "
        "VALUES (?1,?2,?3,?4,?5,?6,COALESCE(NULLIF(?7,''),strftime('%Y-%m-%dT%H:%M:%SZ','now'))) "
        "ON CONFLICT(ScopeKey,IdentityKey) DO UPDATE SET "
        "CatalogId=excluded.CatalogId, SourceUrl=excluded.SourceUrl, Name=excluded.Name, "
        "Provider=excluded.Provider;");
    if (!prepared) {
        return prepared.failure();
    }
    SqliteStatement& statement = prepared.value();
    statement.bindText(1, artist.scopeKey);
    statement.bindText(2, identityKey);
    statement.bindText(3, artist.catalogId);
    statement.bindText(4, artist.sourceUrl);
    statement.bindText(5, artist.name);
    statement.bindText(6, artist.provider);
    statement.bindText(7, artist.addedAtUtc);
    return statement.run();
}

Result<void> FollowedArtistRepository::unfollowArtist(const QString& scopeKey, const QString& identityKey)
{
    if (!isValidLookup(scopeKey, identityKey)) {
        return tx::notFound(QStringLiteral("empty or too long followed artist key"));
    }
    auto prepared = m_connection.prepare("DELETE FROM FollowedArtists WHERE ScopeKey=?1 AND IdentityKey=?2;");
    if (!prepared) {
        return prepared.failure();
    }
    prepared.value().bindText(1, scopeKey);
    prepared.value().bindText(2, identityKey);
    return prepared.value().run();
}

bool FollowedArtistRepository::isArtistFollowed(const QString& scopeKey, const QString& identityKey) const
{
    if (!isValidLookup(scopeKey, identityKey)) {
        return false;
    }
    auto prepared =
        m_connection.prepare("SELECT 1 FROM FollowedArtists WHERE ScopeKey=?1 AND IdentityKey=?2 LIMIT 1;");
    if (!prepared) {
        return false;
    }
    prepared.value().bindText(1, scopeKey);
    prepared.value().bindText(2, identityKey);
    return prepared.value().step().valueOr(false);
}

QList<FollowedArtist> FollowedArtistRepository::loadFollowedArtists(const QString& scopeKey) const
{
    QList<FollowedArtist> artists;
    if (scopeKey.isEmpty()) {
        return artists;
    }
    auto prepared = m_connection.prepare(
        "SELECT ScopeKey, IdentityKey, CatalogId, SourceUrl, Name, Provider, AddedAtUtc "
        "FROM FollowedArtists WHERE ScopeKey=? ORDER BY AddedAtUtc DESC, Name COLLATE NOCASE ASC;");
    if (!prepared) {
        return artists;
    }
    SqliteStatement& statement = prepared.value();
    statement.bindText(1, scopeKey);
    while (statement.step().valueOr(false)) {
        FollowedArtist artist;
        artist.scopeKey = statement.columnText(0);
        artist.identityKey = statement.columnText(1);
        artist.catalogId = statement.columnText(2);
        artist.sourceUrl = statement.columnText(3);
        artist.name = statement.columnText(4);
        artist.provider = statement.columnText(5);
        artist.addedAtUtc = statement.columnText(6);
        artists.append(std::move(artist));
    }
    return artists;
}

} // namespace lmp::core::db
