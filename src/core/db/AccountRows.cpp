#include "core/db/AccountRows.h"

#include "core/db/SqlTransaction.h"
#include "core/net/ArtworkUrl.h"
#include "core/net/InlineProfileImage.h"
#include "core/net/ProviderUrlCredential.h"
#include "core/net/RemoteUrlPolicy.h"

namespace lmp::core::db::account_rows {

namespace {

namespace tx = sql_transaction;

constexpr char kEnsureProfileSql[] =
    "INSERT INTO AccountProfiles (AccountId, UpdatedAtUtc) VALUES (?, datetime('now')) "
    "ON CONFLICT(AccountId) DO NOTHING;";

constexpr char kUpsertTrackMetadataSql[] =
    "INSERT INTO AccountTracks (AccountId, RemoteId, SourceUrl, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, "
    "DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteProvider, RemoteCatalogId, AlbumCatalogId, ArtistCatalogId, UpdatedAtUtc) "
    "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, datetime('now')) "
    "ON CONFLICT(AccountId, RemoteId) DO UPDATE SET "
    "SourceUrl=excluded.SourceUrl, "
    "Title=CASE WHEN excluded.Title<>'' THEN excluded.Title ELSE AccountTracks.Title END, "
    "Artist=CASE WHEN excluded.Artist<>'' THEN excluded.Artist ELSE AccountTracks.Artist END, "
    "Album=CASE WHEN excluded.Album<>'' THEN excluded.Album ELSE AccountTracks.Album END, "
    "Genre=CASE WHEN excluded.Genre<>'' THEN excluded.Genre ELSE AccountTracks.Genre END, "
    "DurationSeconds=CASE WHEN excluded.DurationSeconds>0 THEN excluded.DurationSeconds ELSE AccountTracks.DurationSeconds END, "
    "ArtworkUrl=CASE WHEN excluded.ArtworkUrl<>'' THEN excluded.ArtworkUrl ELSE AccountTracks.ArtworkUrl END, "
    "DateAddedSortKey=CASE WHEN excluded.DateAddedSortKey>0 THEN excluded.DateAddedSortKey ELSE AccountTracks.DateAddedSortKey END, "
    "DateAddedText=CASE WHEN excluded.DateAddedText<>'' THEN excluded.DateAddedText ELSE AccountTracks.DateAddedText END, "
    "DurationText=CASE WHEN excluded.DurationText<>'' THEN excluded.DurationText ELSE AccountTracks.DurationText END, "
    "RemoteProvider=CASE WHEN excluded.RemoteProvider<>'' THEN excluded.RemoteProvider ELSE AccountTracks.RemoteProvider END, "
    "RemoteCatalogId=CASE WHEN excluded.RemoteCatalogId<>'' THEN excluded.RemoteCatalogId ELSE AccountTracks.RemoteCatalogId END, "
    "AlbumCatalogId=CASE WHEN excluded.AlbumCatalogId<>'' THEN excluded.AlbumCatalogId ELSE AccountTracks.AlbumCatalogId END, "
    "ArtistCatalogId=CASE WHEN excluded.ArtistCatalogId<>'' THEN excluded.ArtistCatalogId ELSE AccountTracks.ArtistCatalogId END, "
    "UpdatedAtUtc=datetime('now');";

} // namespace

QString sanitizeAccountUrl(const QString& value)
{
    QString cleaned = net::removeLegacyProviderUrlCredential(value);
    return net::isSafeRemoteUrl(cleaned, net::RemoteUrlUse::Durable) ? cleaned : QString();
}

QString sanitizeAccountArtworkUrl(const QString& value)
{
    return net::durableArtworkUrl(net::removeLegacyProviderUrlCredential(value));
}

QString sanitizeAccountAvatar(const QString& value)
{
    if (net::isSafeInlineProfileImage(value)) {
        return value;
    }
    return sanitizeAccountUrl(value);
}

int columnInt(const SqliteStatement& statement, int column)
{
    return static_cast<int>(statement.columnInt64(column));
}

Track accountTrackFromStatement(const SqliteStatement& statement, int firstColumn)
{
    Track track;
    track.rowId = -statement.columnInt64(firstColumn);
    track.remoteId = statement.columnText(firstColumn + 1);
    track.sourceUrl = statement.columnText(firstColumn + 2);
    track.title = statement.columnText(firstColumn + 3);
    track.artist = statement.columnText(firstColumn + 4);
    track.album = statement.columnText(firstColumn + 5);
    track.genre = statement.columnText(firstColumn + 6);
    track.durationSeconds = statement.columnDouble(firstColumn + 7);
    track.artworkUrl = statement.columnText(firstColumn + 8);
    track.dateAddedSortKey = statement.columnDouble(firstColumn + 9);
    track.dateAdded = statement.columnText(firstColumn + 10);
    track.durationText = statement.columnText(firstColumn + 11);
    track.liked = columnInt(statement, firstColumn + 12) != 0;
    track.sourceTag = statement.columnText(firstColumn + 13);
    track.catalogId = statement.columnText(firstColumn + 14);
    track.albumCatalogId = statement.columnText(firstColumn + 15);
    track.artistCatalogId = statement.columnText(firstColumn + 16);
    track.sourceKind = QStringLiteral("remote");
    track.origin = QStringLiteral("account");
    track.sourceLabel = QStringLiteral("Account");
    track.filePath.clear();
    return track;
}

Result<void> ensureAccountProfile(SqliteConnection& connection, const QString& accountId)
{
    auto prepared = connection.prepare(kEnsureProfileSql);
    if (!prepared) {
        return prepared.failure();
    }
    prepared.value().bindText(1, accountId);
    return prepared.value().run();
}

Result<void> upsertAccountTrackMetadata(SqliteConnection& connection, const QString& accountId, const Track& track)
{
    if (accountId.isEmpty() || track.remoteId.isEmpty()) {
        return tx::notFound(QStringLiteral("account track needs an account and a remote id"));
    }
    const QString sourceUrl = sanitizeAccountUrl(track.sourceUrl);
    if (sourceUrl.isEmpty()) {
        return tx::notFound(QStringLiteral("account track has no durable source address"));
    }
    if (const Result<void> profile = ensureAccountProfile(connection, accountId); !profile) {
        return profile;
    }

    auto prepared = connection.prepare(kUpsertTrackMetadataSql);
    if (!prepared) {
        return prepared.failure();
    }
    SqliteStatement& statement = prepared.value();
    statement.bindText(1, accountId);
    statement.bindText(2, track.remoteId);
    statement.bindText(3, sourceUrl);
    statement.bindText(4, track.title);
    statement.bindText(5, track.artist);
    statement.bindText(6, track.album);
    statement.bindText(7, track.genre);
    statement.bindDouble(8, track.durationSeconds);
    statement.bindText(9, sanitizeAccountArtworkUrl(track.artworkUrl));
    statement.bindDouble(10, track.dateAddedSortKey);
    statement.bindText(11, track.dateAdded);
    statement.bindText(12, track.durationText);
    statement.bindInt64(13, track.liked ? 1 : 0);
    statement.bindText(14, track.sourceTag);
    statement.bindText(15, track.catalogId);
    statement.bindText(16, track.albumCatalogId);
    statement.bindText(17, track.artistCatalogId);
    return statement.run();
}

Result<void> commitOrRollback(SqliteConnection& connection)
{
    if (Result<void> committed = connection.exec("COMMIT;"); !committed) {
        return tx::rollbackWith(connection, committed.failure());
    }
    return Result<void>::success();
}

} // namespace lmp::core::db::account_rows
