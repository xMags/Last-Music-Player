#include "core/db/CollectionRepository.h"

#include "core/db/CollectionSql.h"
#include "core/db/SqlTransaction.h"
#include "core/net/RemoteUrlPolicy.h"

#include <QLatin1StringView>

#include <chrono>
#include <initializer_list>
#include <ratio>

// Album collections, creating and replacing playlists. The playlist edits
// are in CollectionRepositoryPlaylists.cpp.
namespace lmp::core::db {

namespace {

namespace tx = sql_transaction;

constexpr QLatin1StringView kManual{"manual"};

constexpr char kDeleteImportedAlbumTracksSql[] =
    "DELETE FROM Tracks WHERE SourceKind='remote' AND Id IN ("
    "SELECT at.TrackId FROM AlbumTracks at "
    "WHERE at.AlbumId=?1 AND NOT EXISTS ("
    "SELECT 1 FROM AlbumTracks other WHERE other.TrackId=at.TrackId AND other.AlbumId<>?1) "
    "AND NOT EXISTS (SELECT 1 FROM PlaylistTracks pt WHERE pt.TrackId=at.TrackId));";

// The time now in 100 ns ticks since 1970, the unit the system clock used in
// earlier builds, so new keys keep their shape.
QString timestampKeyPart()
{
    using Ticks = std::chrono::duration<qint64, std::ratio<1, 10'000'000>>;
    const auto ticks = std::chrono::duration_cast<Ticks>(std::chrono::system_clock::now().time_since_epoch());
    return QString::number(ticks.count());
}

// 64-bit FNV-1a over the title's UTF-16 code units, low byte first: the value
// the standard string hash gave in earlier builds. It is stored only as part
// of a key, where nothing but uniqueness matters, but keeping it means a
// title hashes the same as before.
QString titleHashKeyPart(const QString& title)
{
    constexpr quint64 kOffsetBasis = 14695981039346656037ULL;
    constexpr quint64 kPrime = 1099511628211ULL;
    quint64 hash = kOffsetBasis;
    for (const QChar character : title) {
        const char16_t unit = character.unicode();
        hash ^= static_cast<quint64>(unit & 0xFFU);
        hash *= kPrime;
        hash ^= static_cast<quint64>(unit >> 8U);
        hash *= kPrime;
    }
    return QString::number(hash);
}

QString manualCollectionKey(const QString& title)
{
    return QStringLiteral("manual|") + timestampKeyPart() + QLatin1Char('|') + titleHashKeyPart(title);
}

QString valueOr(const QString& value, const QString& fallback)
{
    return value.isEmpty() ? fallback : value;
}

} // namespace

CollectionRepository::CollectionRepository(Database& database)
    : m_connection(database.connection())
{
}

qint64 CollectionRepository::lookupId(const char* sql, const QString& key)
{
    auto lookup = m_connection.prepare(sql);
    if (!lookup) {
        return 0;
    }
    lookup.value().bindText(1, key);
    return lookup.value().step().valueOr(false) ? lookup.value().columnInt64(0) : 0;
}

QString CollectionRepository::playlistKey(qint64 playlistId)
{
    auto lookup = m_connection.prepare("SELECT PlaylistKey FROM Playlists WHERE Id=?;");
    if (!lookup || playlistId <= 0) {
        return {};
    }
    lookup.value().bindInt64(1, playlistId);
    return lookup.value().step().valueOr(false) ? lookup.value().columnText(0) : QString();
}

QString CollectionRepository::albumKey(qint64 albumId)
{
    auto lookup = m_connection.prepare("SELECT AlbumKey FROM Albums WHERE Id=?;");
    if (!lookup || albumId <= 0) {
        return {};
    }
    lookup.value().bindInt64(1, albumId);
    return lookup.value().step().valueOr(false) ? lookup.value().columnText(0) : QString();
}

Result<qint64> CollectionRepository::createAlbumCollection(const QString& title)
{
    const QString cleanedTitle = valueOr(title, QStringLiteral("Untitled Album"));
    Track album;
    album.title = cleanedTitle;
    album.artist = QStringLiteral("Your hand-picked songs live here.");
    album.sourceKind = kManual;
    album.sourceLabel = QStringLiteral("Manual");
    return upsertAlbumCollection(album, manualCollectionKey(cleanedTitle));
}

Result<qint64> CollectionRepository::upsertAlbumCollection(const Track& album, const QString& albumKey)
{
    if (albumKey.isEmpty()) {
        return tx::notFound(QStringLiteral("empty album key"));
    }
    auto prepared = m_connection.prepare(collection_sql::UpsertAlbumSql);
    if (!prepared) {
        return prepared.failure();
    }
    SqliteStatement& statement = prepared.value();

    QString artworkUrl = album.artworkUrl;
    if (album.sourceKind != kManual && !net::isSafeRemoteUrl(artworkUrl, net::RemoteUrlUse::Durable)) {
        artworkUrl.clear();
    }
    statement.bindText(1, albumKey);
    statement.bindText(2, album.title);
    statement.bindText(3, album.artist);
    statement.bindText(4, artworkUrl);
    statement.bindText(5, valueOr(album.sourceKind, kManual));
    statement.bindText(6, album.sourceUrl);
    statement.bindText(7, valueOr(album.sourceLabel, QStringLiteral("Album")));
    if (const Result<void> ran = statement.run(); !ran) {
        return ran.failure();
    }

    auto lookup = m_connection.prepare(collection_sql::AlbumIdByKeySql);
    if (!lookup) {
        return lookup.failure();
    }
    lookup.value().bindText(1, albumKey);
    if (lookup.value().step().valueOr(false)) {
        return lookup.value().columnInt64(0);
    }
    return m_connection.lastInsertRowId();
}

Result<void> CollectionRepository::replaceAlbumCollectionTracks(qint64 albumId, const QList<qint64>& trackIds)
{
    return replaceMembers("DELETE FROM AlbumTracks WHERE AlbumId=?;",
                          "INSERT OR IGNORE INTO AlbumTracks (AlbumId, TrackId, TrackOrder) VALUES (?, ?, ?);",
                          "UPDATE Albums SET UpdatedAt=strftime('%s','now') WHERE Id=?;",
                          albumId,
                          trackIds);
}

Result<void> CollectionRepository::replacePlaylistTracks(qint64 playlistId, const QList<qint64>& trackIds)
{
    return replaceMembers("DELETE FROM PlaylistTracks WHERE PlaylistId=?;",
                          "INSERT OR IGNORE INTO PlaylistTracks (PlaylistId, TrackId, TrackOrder) VALUES (?, ?, ?);",
                          "UPDATE Playlists SET UpdatedAt=strftime('%s','now') WHERE Id=?;",
                          playlistId,
                          trackIds);
}

Result<void> CollectionRepository::replaceMembers(const char* deleteSql,
                                                  const char* insertSql,
                                                  const char* touchSql,
                                                  qint64 collectionId,
                                                  const QList<qint64>& trackIds)
{
    if (collectionId <= 0) {
        return Result<void>::success();
    }
    if (const Result<void> begun = m_connection.exec("BEGIN IMMEDIATE;"); !begun) {
        return begun;
    }

    auto remove = m_connection.prepare(deleteSql);
    if (!remove) {
        return tx::rollbackWith(m_connection, remove.failure());
    }
    remove.value().bindInt64(1, collectionId);
    if (const Result<void> ran = remove.value().run(); !ran) {
        return tx::rollbackWith(m_connection, ran.failure());
    }

    auto insert = m_connection.prepare(insertSql);
    if (!insert) {
        return tx::rollbackWith(m_connection, insert.failure());
    }
    int order = 1;
    for (const qint64 trackId : trackIds) {
        if (trackId <= 0) {
            continue;
        }
        insert.value().reset();
        insert.value().clearBindings();
        insert.value().bindInt64(1, collectionId);
        insert.value().bindInt64(2, trackId);
        insert.value().bindInt64(3, order++);
        if (const Result<void> ran = insert.value().run(); !ran) {
            return tx::rollbackWith(m_connection, ran.failure());
        }
    }

    // Touching the timestamp is best effort, as before.
    if (auto touch = m_connection.prepare(touchSql); touch) {
        touch.value().bindInt64(1, collectionId);
        static_cast<void>(touch.value().step());
    }
    if (const Result<void> committed = m_connection.exec("COMMIT;"); !committed) {
        return tx::rollbackWith(m_connection, committed.failure());
    }
    return Result<void>::success();
}

Result<void> CollectionRepository::deleteAlbumCollection(const QString& albumKey)
{
    if (albumKey.isEmpty()) {
        return tx::notFound(QStringLiteral("empty album key"));
    }

    auto lookup = m_connection.prepare("SELECT Id, SourceKind FROM Albums WHERE AlbumKey=?;");
    if (!lookup) {
        return lookup.failure();
    }
    lookup.value().bindText(1, albumKey);
    if (!lookup.value().step().valueOr(false)) {
        return tx::notFound(QStringLiteral("no album with that key"));
    }
    const qint64 albumId = lookup.value().columnInt64(0);
    const QString sourceKind = lookup.value().columnText(1).toLower();
    if (albumId <= 0) {
        return tx::notFound(QStringLiteral("album id is not positive"));
    }

    if (const Result<void> begun = m_connection.exec("BEGIN IMMEDIATE;"); !begun) {
        return begun;
    }

    if (sourceKind != kManual) {
        auto deleteImportedTracks = m_connection.prepare(kDeleteImportedAlbumTracksSql);
        if (!deleteImportedTracks) {
            return tx::rollbackWith(m_connection, deleteImportedTracks.failure());
        }
        deleteImportedTracks.value().bindInt64(1, albumId);
        if (const Result<void> ran = deleteImportedTracks.value().run(); !ran) {
            return tx::rollbackWith(m_connection, ran.failure());
        }
    }

    for (const char* sql : {"DELETE FROM AlbumTracks WHERE AlbumId=?;", "DELETE FROM Albums WHERE Id=?;"}) {
        auto statement = m_connection.prepare(sql);
        if (!statement) {
            return tx::rollbackWith(m_connection, statement.failure());
        }
        statement.value().bindInt64(1, albumId);
        if (const Result<void> ran = statement.value().run(); !ran) {
            return tx::rollbackWith(m_connection, ran.failure());
        }
    }

    if (const Result<void> committed = m_connection.exec("COMMIT;"); !committed) {
        return tx::rollbackWith(m_connection, committed.failure());
    }
    return Result<void>::success();
}

Result<qint64> CollectionRepository::createPlaylist(const QString& title)
{
    const QString cleanedTitle = valueOr(title, QStringLiteral("Untitled Playlist"));
    Track playlist;
    playlist.title = cleanedTitle;
    playlist.artist = QStringLiteral("Add songs from your library.");
    playlist.sourceKind = kManual;
    playlist.origin = kManual;
    playlist.sourceLabel = QStringLiteral("Manual");
    return upsertPlaylist(playlist, manualCollectionKey(cleanedTitle));
}

Result<qint64> CollectionRepository::upsertPlaylist(const Track& playlist, const QString& playlistKey)
{
    if (playlistKey.isEmpty()) {
        return tx::notFound(QStringLiteral("empty playlist key"));
    }
    auto prepared = m_connection.prepare(collection_sql::UpsertPlaylistSql);
    if (!prepared) {
        return prepared.failure();
    }
    SqliteStatement& statement = prepared.value();

    QString artworkUrl = playlist.artworkUrl;
    if (playlist.origin != kManual && !net::isSafeRemoteUrl(artworkUrl, net::RemoteUrlUse::Durable)) {
        artworkUrl.clear();
    }
    statement.bindText(1, playlistKey);
    statement.bindText(2, playlist.title);
    // A playlist's description travels in the artist field.
    statement.bindText(3, playlist.artist);
    statement.bindText(4, artworkUrl);
    statement.bindText(5, valueOr(playlist.sourceKind, kManual));
    statement.bindText(6, valueOr(playlist.origin, kManual));
    statement.bindText(7, playlist.sourceUrl);
    statement.bindText(8, valueOr(playlist.sourceLabel, QStringLiteral("Manual")));
    if (const Result<void> ran = statement.run(); !ran) {
        return ran.failure();
    }

    auto lookup = m_connection.prepare(collection_sql::PlaylistIdByKeySql);
    if (!lookup) {
        return lookup.failure();
    }
    lookup.value().bindText(1, playlistKey);
    if (lookup.value().step().valueOr(false)) {
        return lookup.value().columnInt64(0);
    }
    return m_connection.lastInsertRowId();
}

} // namespace lmp::core::db
