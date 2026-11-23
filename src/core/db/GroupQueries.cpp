#include "core/db/GroupQueries.h"

#include "core/db/LibraryGroupingSql.h"
#include "core/db/TrackRows.h"
#include "core/model/ArtistIdentity.h"

#include <QChar>
#include <QLatin1StringView>
#include <QString>

#include <algorithm>
#include <string>
#include <utility>

namespace lmp::core::db {

namespace {

namespace grouping = library_grouping_sql;

// "1 song", "2 songs".
[[nodiscard]] QString counted(int count, QLatin1StringView singular, QLatin1StringView plural)
{
    return QString::number(count) + QLatin1Char(' ') + (count == 1 ? singular : plural);
}

constexpr QLatin1StringView kAccount{"account"};

constexpr char kAlbumCollectionsSql[] =
    "SELECT Id, AlbumKey, Title, Artist, ArtworkUrl, SourceKind, SourceUrl, SourceLabel, "
    "(SELECT COUNT(*) FROM AlbumTracks WHERE AlbumId=Albums.Id) "
    "FROM Albums ORDER BY UpdatedAt DESC, Title COLLATE NOCASE ASC;";

constexpr char kPlaylistsSql[] =
    "SELECT Id, PlaylistKey, Title, Description, ArtworkUrl, SourceKind, Provider, SourceUrl, SourceLabel, "
    "(SELECT COUNT(*) FROM PlaylistTracks WHERE PlaylistId=Playlists.Id), "
    "COALESCE(NULLIF(UpdatedAt,0),CreatedAt,0) "
    "FROM Playlists ORDER BY UpdatedAt DESC, Title COLLATE NOCASE ASC;";

constexpr char kAccountPlaylistsSql[] =
    "SELECT p.PlaylistId, p.Name, p.Description, p.SourceUrl, "
    "(SELECT COUNT(*) FROM AccountPlaylistTracks pt WHERE pt.AccountId=p.AccountId AND pt.PlaylistId=p.PlaylistId), "
    "COALESCE((SELECT t.ArtworkUrl FROM AccountPlaylistTracks pt JOIN AccountTracks t ON t.AccountId=pt.AccountId AND t.RemoteId=pt.RemoteId "
    "WHERE pt.AccountId=p.AccountId AND pt.PlaylistId=p.PlaylistId AND COALESCE(t.ArtworkUrl,'')<>'' "
    "ORDER BY pt.TrackOrder LIMIT 1),''), "
    "COALESCE(CAST(strftime('%s', p.UpdatedAtUtc) AS INTEGER),0) "
    "FROM AccountPlaylists p JOIN ActiveAccountContext c ON c.SingletonId=1 AND c.AccountId=p.AccountId "
    "ORDER BY p.UpdatedAtUtc DESC, p.Name COLLATE NOCASE ASC;";

// Broad media categories that some remote metadata carries instead of a
// musical genre; they make poor mix labels.
constexpr char kTopGenresSql[] =
    "SELECT CASE WHEN Genre IS NULL OR Genre='' THEN '' ELSE Genre END AS G,"
    "SUM(PlayCount) AS Plays, COUNT(*) AS Cnt "
    "FROM EffectiveTracks WHERE IsActive=1 "
    "GROUP BY G HAVING G <> '' AND LOWER(TRIM(G)) NOT IN ("
    "'music','people & blogs','entertainment','news & politics','education',"
    "'film & animation','gaming','howto & style','science & technology','sports',"
    "'comedy','travel & events','autos & vehicles','pets & animals',"
    "'nonprofits & activism','trailers','shows','movies','remote','other',"
    "'various','various artists','unknown','unknown genre','uncategorized') "
    "ORDER BY Plays DESC, Cnt DESC, G COLLATE NOCASE ASC "
    "LIMIT ?1;";

// The value sqlite3_column_int would give: the low 32 bits of the integer.
int columnInt(const SqliteStatement& statement, int column)
{
    return static_cast<int>(statement.columnInt64(column));
}

QString captionOr(const QString& description, int count)
{
    return description.isEmpty() ? track_rows::songCountText(count) : description;
}

// The identity an artist card stands for. Only an http(s) address counts:
// a card built from song tags keeps its name in sourceUrl.
ArtistIdentity identityOfCard(const Track& group)
{
    return ArtistIdentity{group.artistCatalogId,
                          group.sourceUrl.startsWith(QLatin1StringView("http")) ? group.sourceUrl : QString(),
                          group.title,
                          group.sourceTag};
}

// Merges the artists followed in `scopeKey` into the cards: see
// GroupQueries::loadArtists. Cards appended here are matched by later
// followed rows too, as before.
void applyFollowedArtists(SqliteConnection& connection, QList<Track>& groups, const QString& scopeKey)
{
    auto followed = connection.prepare(
        "SELECT IdentityKey, CatalogId, SourceUrl, Name, Provider, AddedAtUtc, "
        "CAST(strftime('%s',AddedAtUtc) AS REAL) "
        "FROM FollowedArtists WHERE ScopeKey=? ORDER BY AddedAtUtc DESC, Name COLLATE NOCASE ASC;");
    if (!followed) {
        return;
    }
    SqliteStatement& statement = followed.value();
    statement.bindText(1, scopeKey);
    while (statement.step().valueOr(false)) {
        const ArtistIdentity followedIdentity{
            statement.columnText(1), statement.columnText(2), statement.columnText(3), statement.columnText(4)};
        const auto existing =
            std::find_if(groups.begin(), groups.end(), [&followedIdentity](const Track& group) {
                return sameArtistIdentity(identityOfCard(group), followedIdentity);
            });
        if (existing != groups.end()) {
            existing->followed = true;
            existing->dateAdded = statement.columnText(5);
            existing->dateAddedSortKey = statement.columnDouble(6);
            // Two spaces, U+00B7 MIDDLE DOT, two spaces.
            existing->artist += QStringLiteral("  ") + QChar(char16_t{0x00B7}) + QStringLiteral("  Following");
            continue;
        }

        Track group;
        group.title = followedIdentity.name;
        group.artist = QStringLiteral("Following");
        group.sourceKind = QStringLiteral("artist");
        group.sourceUrl = followedIdentity.sourceUrl.isEmpty() ? followedIdentity.name : followedIdentity.sourceUrl;
        group.artistCatalogId = followedIdentity.catalogId;
        group.sourceTag = followedIdentity.provider;
        group.sourceLabel = QStringLiteral("Remote");
        group.followed = true;
        group.dateAdded = statement.columnText(5);
        group.dateAddedSortKey = statement.columnDouble(6);
        groups.append(std::move(group));
    }
}

} // namespace

GroupQueries::GroupQueries(Database& database)
    : m_connection(database.connection())
{
}

QList<Track> GroupQueries::loadAlbums()
{
    QList<Track> groups;
    if (auto collections = m_connection.prepare(kAlbumCollectionsSql); collections) {
        SqliteStatement& statement = collections.value();
        while (statement.step().valueOr(false)) {
            Track group;
            group.rowId = statement.columnInt64(0);
            const QString collectionSourceKind = statement.columnText(5);
            QString artist = statement.columnText(3);
            if (collectionSourceKind.toLower() == QLatin1StringView("manual")
                && (artist.isEmpty() || artist == QLatin1StringView("Manual album"))) {
                artist = QStringLiteral("Your hand-picked songs live here.");
            }
            group.sourceUrl = statement.columnText(1);
            group.title = statement.columnText(2);
            group.artist = artist;
            group.artworkUrl = statement.columnText(4);
            group.origin = collectionSourceKind;
            group.sourceKind = QStringLiteral("album-collection");
            const QString sourceLabel = statement.columnText(7);
            group.sourceLabel = sourceLabel.isEmpty() ? QStringLiteral("Album") : sourceLabel;
            group.trackCount = columnInt(statement, 8);
            groups.append(std::move(group));
        }
    }

    const std::string sql = grouping::EligibleTracksCte(grouping::GroupKind::Album)
        + "SELECT GroupTitle,"
          "MIN(CASE WHEN COALESCE(TRIM(Artist),'')='' THEN 'Unknown Artist' ELSE TRIM(Artist) END),"
          "MAX(COALESCE(ArtworkUrl,'')),"
          "COUNT(*), SUM(CASE WHEN SourceKind='remote' THEN 1 ELSE 0 END) "
          "FROM EligibleLibraryTracks WHERE GroupTitle<>'' AND Id NOT IN ("
          "SELECT at.TrackId FROM AlbumTracks at JOIN Albums a ON a.Id=at.AlbumId "
          "WHERE a.SourceKind<>'manual') "
          "GROUP BY GroupTitle COLLATE NOCASE "
          "ORDER BY COUNT(*) DESC, GroupTitle COLLATE NOCASE ASC;";
    auto derived = m_connection.prepare(sql.c_str());
    if (!derived) {
        return groups;
    }
    SqliteStatement& statement = derived.value();
    while (statement.step().valueOr(false)) {
        Track group;
        const int count = columnInt(statement, 3);
        const int remoteCount = columnInt(statement, 4);
        group.title = statement.columnText(0);
        group.artist = statement.columnText(1);
        group.artworkUrl = statement.columnText(2);
        group.trackCount = count;
        group.sourceKind = QStringLiteral("album");
        group.sourceUrl = group.title;
        group.sourceLabel = track_rows::mixLabelFor(remoteCount, count);
        groups.append(std::move(group));
    }
    return groups;
}

QList<Track> GroupQueries::loadPlaylists()
{
    QList<Track> groups;
    auto local = m_connection.prepare(kPlaylistsSql);
    if (!local) {
        return groups;
    }
    while (local.value().step().valueOr(false)) {
        const SqliteStatement& statement = local.value();
        Track group;
        const int count = columnInt(statement, 9);
        group.rowId = statement.columnInt64(0);
        group.sourceUrl = statement.columnText(1);
        group.title = statement.columnText(2);
        group.artist = captionOr(statement.columnText(3), count);
        group.artworkUrl = statement.columnText(4);
        group.origin = statement.columnText(6);
        group.sourceKind = QStringLiteral("playlist");
        group.sourceLabel = QStringLiteral("On this PC");
        group.trackCount = count;
        group.dateAddedSortKey = statement.columnDouble(10);
        groups.append(std::move(group));
    }

    auto synced = m_connection.prepare(kAccountPlaylistsSql);
    if (!synced) {
        return groups;
    }
    while (synced.value().step().valueOr(false)) {
        const SqliteStatement& statement = synced.value();
        Track group;
        const QString playlistId = statement.columnText(0);
        const int count = columnInt(statement, 4);
        group.remoteId = playlistId;
        group.sourceUrl = QStringLiteral("account-playlist|") + playlistId;
        group.title = statement.columnText(1);
        group.artist = captionOr(statement.columnText(2), count);
        group.artworkUrl = statement.columnText(5);
        group.origin = kAccount;
        group.sourceKind = QStringLiteral("playlist");
        group.sourceLabel = QStringLiteral("Synced");
        group.trackCount = count;
        group.dateAddedSortKey = statement.columnDouble(6);
        groups.append(std::move(group));
    }
    return groups;
}

QList<Track> GroupQueries::loadRecentPlaylists(int limit)
{
    QList<Track> groups = loadPlaylists();
    std::stable_sort(groups.begin(), groups.end(), [](const Track& left, const Track& right) {
        const bool leftAccount = left.origin == kAccount;
        const bool rightAccount = right.origin == kAccount;
        if (leftAccount != rightAccount) {
            return leftAccount;
        }
        return left.title < right.title;
    });
    if (limit <= 0) {
        return {};
    }
    if (groups.size() > limit) {
        groups.resize(limit);
    }
    return groups;
}

QList<Track> GroupQueries::loadArtists(const QString& followedScopeKey)
{
    QList<Track> groups;
    const std::string sql = grouping::EligibleTracksCte(grouping::GroupKind::Artist)
        + "SELECT GroupTitle, COUNT(*), "
          "COUNT(DISTINCT CASE WHEN COALESCE(TRIM(Album),'')='' "
          "OR LOWER(TRIM(Album))='unknown album' THEN NULL "
          "ELSE LOWER(TRIM(Album)) END),"
          "MAX(COALESCE(ArtworkUrl,'')),"
          "SUM(CASE WHEN SourceKind='remote' THEN 1 ELSE 0 END),"
          "CASE WHEN COUNT(DISTINCT NULLIF(ArtistCatalogId,''))=1 "
          "THEN MAX(COALESCE(ArtistCatalogId,'')) ELSE '' END, "
          "MAX(COALESCE(NULLIF(RemoteProvider,''),NULLIF(Provider,''),'')),"
          "MAX(CASE WHEN Provider='account' THEN 'account' ELSE '' END) "
          "FROM EligibleLibraryTracks WHERE GroupTitle<>'' "
          "GROUP BY GroupTitle COLLATE NOCASE "
          "ORDER BY COUNT(*) DESC, GroupTitle COLLATE NOCASE ASC;";
    auto prepared = m_connection.prepare(sql.c_str());
    if (!prepared) {
        return groups;
    }
    SqliteStatement& statement = prepared.value();
    while (statement.step().valueOr(false)) {
        Track group;
        const int count = columnInt(statement, 1);
        const int albums = columnInt(statement, 2);
        const int remoteCount = columnInt(statement, 4);
        group.title = statement.columnText(0);
        group.artist = counted(count, QLatin1StringView("song"), QLatin1StringView("songs")) + QStringLiteral(" - ")
            + counted(albums, QLatin1StringView("album"), QLatin1StringView("albums"));
        group.artworkUrl = statement.columnText(3);
        group.trackCount = count;
        group.inLibrary = true;
        group.sourceKind = QStringLiteral("artist");
        group.sourceUrl = group.title;
        group.sourceLabel = track_rows::mixLabelFor(remoteCount, count);
        group.artistCatalogId = statement.columnText(5);
        group.sourceTag = statement.columnText(6);
        group.origin = statement.columnText(7);
        groups.append(std::move(group));
    }

    if (!followedScopeKey.isEmpty()) {
        applyFollowedArtists(m_connection, groups, followedScopeKey);
    }
    return groups;
}

QList<Track> GroupQueries::loadGenres()
{
    QList<Track> groups;
    const std::string sql = grouping::EligibleTracksCte(grouping::GroupKind::Genre)
        + "SELECT GroupTitle, COUNT(*), "
          "SUM(CASE WHEN SourceKind='remote' THEN 1 ELSE 0 END) "
          "FROM EligibleLibraryTracks WHERE GroupTitle<>'' "
          "GROUP BY GroupTitle COLLATE NOCASE "
          "ORDER BY COUNT(*) DESC, GroupTitle COLLATE NOCASE ASC;";
    auto prepared = m_connection.prepare(sql.c_str());
    if (!prepared) {
        return groups;
    }
    SqliteStatement& statement = prepared.value();
    while (statement.step().valueOr(false)) {
        Track group;
        const int count = columnInt(statement, 1);
        const int remoteCount = columnInt(statement, 2);
        group.title = statement.columnText(0);
        group.artist = track_rows::songCountText(count);
        group.trackCount = count;
        group.sourceKind = QStringLiteral("genre");
        group.sourceUrl = group.title;
        group.sourceLabel = track_rows::mixLabelFor(remoteCount, count);
        groups.append(std::move(group));
    }
    return groups;
}

QStringList GroupQueries::loadTopGenres(int limit)
{
    QStringList genres;
    if (limit <= 0) {
        return genres;
    }
    auto statement = m_connection.prepare(kTopGenresSql);
    if (!statement) {
        return genres;
    }
    statement.value().bindInt64(1, limit);
    while (statement.value().step().valueOr(false)) {
        QString name = statement.value().columnText(0);
        if (!name.isEmpty()) {
            genres.append(std::move(name));
        }
    }
    return genres;
}

} // namespace lmp::core::db
