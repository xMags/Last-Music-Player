#include "core/db/LibraryQueries.h"

#include "core/db/LibraryGroupingSql.h"
#include "core/db/TrackRows.h"

#include <QLatin1StringView>

#include <algorithm>
#include <string>
#include <utility>

namespace lmp::core::db {

namespace {

namespace grouping = library_grouping_sql;

constexpr char kLoadTracksSql[] =
    "SELECT Id, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteId, LastPositionSeconds, RemoteProvider, RemoteCatalogId, AlbumCatalogId, ArtistCatalogId "
    "FROM EffectiveTracks "
    "WHERE (?1=1 OR SourceKind='local') AND (?2=0 OR IsActive=1) "
    "ORDER BY DateAddedSortKey DESC, Title COLLATE NOCASE ASC;";

constexpr char kLoadHistoryTracksSql[] =
    "SELECT Id, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteId, LastPositionSeconds, RemoteProvider, RemoteCatalogId, AlbumCatalogId, ArtistCatalogId "
    "FROM EffectiveTracks "
    "WHERE (?1=1 OR SourceKind='local') AND (?2=0 OR IsActive=1) AND COALESCE(LastPlayedOrder,0)>0 "
    "ORDER BY LastPlayedOrder DESC, Title COLLATE NOCASE ASC;";

constexpr char kLoadMostPlayedTracksSql[] =
    "SELECT Id, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteId, LastPositionSeconds, RemoteProvider, RemoteCatalogId, AlbumCatalogId, ArtistCatalogId "
    "FROM EffectiveTracks "
    "WHERE (?1=1 OR SourceKind='local') AND (?2=0 OR IsActive=1) AND COALESCE(PlayCount,0)>0 "
    "ORDER BY PlayCount DESC, LastPlayedOrder DESC, Title COLLATE NOCASE ASC;";

constexpr char kHistoryImportCandidatesSql[] =
    "SELECT Id, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, "
    "DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteId, LastPositionSeconds, '', RemoteCatalogId, AlbumCatalogId, ArtistCatalogId, SourceKey, LastPlayed "
    "FROM Tracks WHERE SourceKind='remote' AND COALESCE(PlayCount,0)>0 AND COALESCE(LastPlayedExact,0)=1 "
    "AND COALESCE(SourceUrl,'')<>'' AND COALESCE(LastPlayed,'')<>'' "
    "ORDER BY LastPlayed DESC, Id DESC;";

// The shelf's own rule: a song that was never added to the library, only
// passed through in a search result, holds zero and stays out.
constexpr char kRecentlyAddedSql[] =
    "SELECT Id, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteId, LastPositionSeconds, RemoteProvider, RemoteCatalogId, AlbumCatalogId, ArtistCatalogId "
    "FROM EffectiveTracks "
    "WHERE IsActive=1 AND COALESCE(DateAddedSortKey,0)<>0 "
    "ORDER BY DateAddedSortKey DESC, Title COLLATE NOCASE ASC "
    "LIMIT ?1;";

constexpr char kLibraryStatsSql[] =
    "SELECT COUNT(*), "
    "(SELECT COUNT(*) FROM Albums) + COUNT(DISTINCT CASE WHEN Id NOT IN (SELECT at.TrackId FROM AlbumTracks at JOIN Albums a ON a.Id=at.AlbumId WHERE a.SourceKind<>'manual') THEN CASE WHEN Album IS NULL OR Album='' THEN CASE WHEN SourceKind='remote' THEN 'Remote Singles' ELSE 'Unknown Album' END ELSE Album END END), "
    "COUNT(DISTINCT CASE WHEN Artist IS NULL OR Artist='' THEN 'Unknown Artist' ELSE Artist END), "
    "COUNT(DISTINCT CASE WHEN Genre IS NULL OR Genre='' THEN 'Unknown Genre' ELSE Genre END), "
    "COALESCE(SUM(DurationSeconds),0) FROM EffectiveTracks WHERE IsActive=1;";

constexpr QLatin1StringView kAccountPlaylistPrefix{"account-playlist|"};

// The value sqlite3_column_int would give: the low 32 bits of the integer.
int columnInt(const SqliteStatement& statement, int column)
{
    return static_cast<int>(statement.columnInt64(column));
}

struct Totals {
    int count = 0;
    double seconds = 0.0;
};

// Runs a COUNT/SUM query; zeros when it fails.
template <typename Binder>
Totals readTotals(SqliteConnection& connection, const std::string& sql, const Binder& binder)
{
    auto statement = connection.prepare(sql.c_str());
    if (!statement) {
        return {};
    }
    binder(statement.value());
    if (!statement.value().step().valueOr(false)) {
        return {};
    }
    return Totals{columnInt(statement.value(), 0), statement.value().columnDouble(1)};
}

template <typename Binder>
QList<Track> readPage(SqliteConnection& connection, const std::string& sql, const Binder& binder, int firstIndex)
{
    auto statement = connection.prepare(sql.c_str());
    if (!statement) {
        return {};
    }
    binder(statement.value());
    return track_rows::readTracks(statement.value(), firstIndex);
}

void bindLimitOffset(SqliteStatement& statement, int firstIndex, int limit, int offset)
{
    if (limit > 0) {
        statement.bindInt64(firstIndex, limit);
        statement.bindInt64(firstIndex + 1, offset);
    }
}

} // namespace

LibraryQueries::LibraryQueries(Database& database, const AccountPlaylistTrackSource* accountPlaylists)
    : m_connection(database.connection())
    , m_accountPlaylists(accountPlaylists)
{
}

QList<Track> LibraryQueries::loadSimpleList(const char* sql, bool includeRemote, bool activeOnly)
{
    auto statement = m_connection.prepare(sql);
    if (!statement) {
        return {};
    }
    statement.value().bindInt64(1, includeRemote ? 1 : 0);
    statement.value().bindInt64(2, activeOnly ? 1 : 0);
    return track_rows::readTracks(statement.value());
}

QList<Track> LibraryQueries::loadTracks(bool includeRemote, bool activeOnly)
{
    return loadSimpleList(kLoadTracksSql, includeRemote, activeOnly);
}

QList<Track> LibraryQueries::loadHistoryTracks(bool includeRemote, bool activeOnly)
{
    return loadSimpleList(kLoadHistoryTracksSql, includeRemote, activeOnly);
}

QList<Track> LibraryQueries::loadMostPlayedTracks(bool includeRemote, bool activeOnly)
{
    return loadSimpleList(kLoadMostPlayedTracksSql, includeRemote, activeOnly);
}

QList<HistoryImportCandidate> LibraryQueries::loadLegacyRemoteHistoryImportCandidates()
{
    QList<HistoryImportCandidate> candidates;
    auto statement = m_connection.prepare(kHistoryImportCandidatesSql);
    if (!statement) {
        return candidates;
    }
    while (statement.value().step().valueOr(false)) {
        HistoryImportCandidate candidate;
        candidate.track = track_rows::trackFromStatement(statement.value());
        // Selected after the track columns, so these move with that list.
        candidate.sourceKey = statement.value().columnText(track_rows::kTrackColumnCount);
        candidate.lastPlayedUtc = statement.value().columnText(track_rows::kTrackColumnCount + 1);
        if (!candidate.sourceKey.isEmpty() && !candidate.lastPlayedUtc.isEmpty()) {
            candidates.append(std::move(candidate));
        }
    }
    return candidates;
}

TrackPage LibraryQueries::loadTrackPage(const TrackQuery& query)
{
    const QString groupKind = query.groupKind.toLower();
    if (groupKind == QLatin1StringView("album-collection")) {
        return loadCollectionPage(query, CollectionTable::Albums);
    }
    if (groupKind == QLatin1StringView("playlist") && query.groupKey.startsWith(kAccountPlaylistPrefix)) {
        if (query.accountOwnerId.isEmpty() || m_accountPlaylists == nullptr) {
            return {};
        }
        const QString playlistId = query.groupKey.mid(kAccountPlaylistPrefix.size());
        return pageAccountPlaylistTracks(m_accountPlaylists->loadAccountPlaylistTracks(query.accountOwnerId, playlistId),
                                         query);
    }
    if (groupKind == QLatin1StringView("playlist")) {
        return loadCollectionPage(query, CollectionTable::Playlists);
    }
    return loadFlatPage(query);
}

TrackPage LibraryQueries::loadCollectionPage(const TrackQuery& query, CollectionTable table)
{
    const int offset = std::max(0, query.offset);
    const int limit = query.limit;
    const QString searchPattern = track_rows::escapedLikePattern(query.searchText);
    const std::string collectionSearch = grouping::CollectionSearchClause(query.searchText, 3);
    const int collectionLimitIndex = query.searchText.isEmpty() ? 3 : 4;

    const auto bindCollection = [&query, &searchPattern](SqliteStatement& statement) {
        statement.bindText(1, query.groupKey);
        statement.bindInt64(2, query.activeOnly ? 1 : 0);
        if (!query.searchText.isEmpty()) {
            statement.bindText(3, searchPattern);
        }
    };

    std::string countSql;
    std::string pageSql;
    if (table == CollectionTable::Albums) {
        countSql = std::string(
            "SELECT COUNT(*), COALESCE(SUM(t.DurationSeconds),0) "
            "FROM AlbumTracks at "
            "JOIN Albums a ON a.Id=at.AlbumId "
            "JOIN Tracks t ON t.Id=at.TrackId "
            "WHERE a.AlbumKey=?1 AND (?2=0 OR t.IsActive=1)") + collectionSearch;
        pageSql = std::string("SELECT ") + track_rows::kJoinedTrackColumns +
            " FROM AlbumTracks at "
            "JOIN Albums a ON a.Id=at.AlbumId "
            "JOIN Tracks t ON t.Id=at.TrackId "
            "WHERE a.AlbumKey=?1 AND (?2=0 OR t.IsActive=1)" + collectionSearch + " " +
            grouping::CollectionOrderClause(query.sort, "at.TrackOrder");
    } else {
        countSql = std::string(
            "SELECT COUNT(*), COALESCE(SUM(t.DurationSeconds),0) "
            "FROM PlaylistTracks pt "
            "JOIN Playlists p ON p.Id=pt.PlaylistId "
            "JOIN Tracks t ON t.Id=pt.TrackId "
            "WHERE p.PlaylistKey=?1 AND (?2=0 OR t.IsActive=1)") + collectionSearch;
        pageSql = std::string("SELECT ") + track_rows::kJoinedTrackColumns +
            " FROM PlaylistTracks pt "
            "JOIN Playlists p ON p.Id=pt.PlaylistId "
            "JOIN Tracks t ON t.Id=pt.TrackId "
            "WHERE p.PlaylistKey=?1 AND (?2=0 OR t.IsActive=1)" + collectionSearch + " " +
            grouping::CollectionOrderClause(query.sort, "pt.TrackOrder");
    }
    if (limit > 0) {
        pageSql += " LIMIT ?" + std::to_string(collectionLimitIndex);
        pageSql += " OFFSET ?" + std::to_string(collectionLimitIndex + 1);
    }

    TrackPage page;
    const Totals totals = readTotals(m_connection, countSql, bindCollection);
    page.totalCount = totals.count;
    page.totalSeconds = totals.seconds;
    const auto bindPage = [&bindCollection, collectionLimitIndex, limit, offset](SqliteStatement& statement) {
        bindCollection(statement);
        bindLimitOffset(statement, collectionLimitIndex, limit, offset);
    };
    page.tracks = readPage(m_connection, pageSql, bindPage, offset + 1);
    return page;
}

TrackPage LibraryQueries::loadFlatPage(const TrackQuery& query)
{
    const int offset = std::max(0, query.offset);
    const int limit = query.limit;

    std::string where =
        "FROM EffectiveTracks "
        "WHERE (?1=1 OR SourceKind='local') AND (?2=0 OR IsActive=1)";
    where += track_rows::trackFilterClause(query.filter);
    const std::string groupFilter = track_rows::groupFilterClause(query.groupKind);
    if (query.scope == QLatin1StringView("Account")) {
        where += " AND Provider='account'";
    } else if (query.scope == QLatin1StringView("OnThisPc")) {
        where += " AND Provider<>'account'";
    }
    where += groupFilter;

    int nextParameter = groupFilter.empty() ? 3 : 4;
    int searchParameter = 0;
    QString searchPattern;
    if (!query.searchText.isEmpty()) {
        searchParameter = nextParameter++;
        searchPattern = track_rows::escapedLikePattern(query.searchText);
        const std::string parameter = std::string("?") + std::to_string(searchParameter);
        where += " AND (COALESCE(Title,'') LIKE " + parameter + " ESCAPE '!'";
        where += " OR COALESCE(Artist,'') LIKE " + parameter + " ESCAPE '!'";
        where += " OR COALESCE(Album,'') LIKE " + parameter + " ESCAPE '!')";
    }

    const std::string countSql = std::string("SELECT COUNT(*), COALESCE(SUM(DurationSeconds),0) ") + where;
    std::string pageSql = std::string("SELECT ") + track_rows::kTrackColumns + " " + where + " "
        + track_rows::trackOrderClause(query.filter, query.sort);
    const int limitIndex = nextParameter;
    if (limit > 0) {
        pageSql += " LIMIT ?";
        pageSql += std::to_string(limitIndex);
        pageSql += " OFFSET ?";
        pageSql += std::to_string(limitIndex + 1);
    }

    const bool hasGroupFilter = !groupFilter.empty();
    const auto binder = [&query, &searchPattern, hasGroupFilter, searchParameter](SqliteStatement& statement) {
        statement.bindInt64(1, query.includeRemote ? 1 : 0);
        statement.bindInt64(2, query.activeOnly ? 1 : 0);
        if (hasGroupFilter) {
            statement.bindText(3, query.groupKey);
        }
        if (searchParameter > 0) {
            statement.bindText(searchParameter, searchPattern);
        }
    };

    TrackPage page;
    const Totals totals = readTotals(m_connection, countSql, binder);
    page.totalCount = totals.count;
    page.totalSeconds = totals.seconds;
    const auto bindPage = [&binder, limitIndex, limit, offset](SqliteStatement& statement) {
        binder(statement);
        bindLimitOffset(statement, limitIndex, limit, offset);
    };
    page.tracks = readPage(m_connection, pageSql, bindPage, offset + 1);
    return page;
}

QList<Track> LibraryQueries::loadTracksForQuery(const TrackQuery& query)
{
    TrackQuery fullQuery = query;
    fullQuery.offset = 0;
    fullQuery.limit = 0;
    return loadTrackPage(fullQuery).tracks;
}

QList<Track> LibraryQueries::loadTracksForGroup(const QString& groupKind, const QString& groupKey)
{
    if (groupKey.isEmpty()) {
        return {};
    }
    const QString normalizedKind = groupKind.toLower();
    if (normalizedKind != QLatin1StringView("album") && normalizedKind != QLatin1StringView("artist")
        && normalizedKind != QLatin1StringView("genre") && normalizedKind != QLatin1StringView("album-collection")
        && normalizedKind != QLatin1StringView("playlist")) {
        return {};
    }

    TrackQuery query;
    query.groupKind = normalizedKind;
    query.groupKey = groupKey;
    query.includeRemote = true;
    query.activeOnly = true;
    return loadTracksForQuery(query);
}

QList<Track> LibraryQueries::loadRecentlyAddedTracks(int limit)
{
    auto statement = m_connection.prepare(kRecentlyAddedSql);
    if (!statement) {
        return {};
    }
    statement.value().bindInt64(1, limit > 0 ? limit : -1);
    return track_rows::readTracks(statement.value());
}

LibraryStats LibraryQueries::libraryStats()
{
    LibraryStats stats;
    auto totals = m_connection.prepare(kLibraryStatsSql);
    if (!totals || !totals.value().step().valueOr(false)) {
        return stats;
    }
    stats.songCount = columnInt(totals.value(), 0);
    stats.albumCount = columnInt(totals.value(), 1);
    stats.artistCount = columnInt(totals.value(), 2);
    stats.genreCount = columnInt(totals.value(), 3);
    stats.totalSeconds = totals.value().columnDouble(4);
    return stats;
}

QList<PlaybackStat> LibraryQueries::loadPlaybackStats()
{
    QList<PlaybackStat> result;
    const std::string sql = std::string("SELECT ") + track_rows::kTrackColumns +
        ", SourceKey, COALESCE(PlayCount,0), COALESCE(LastPlayedOrder,0) FROM EffectiveTracks "
        "WHERE IsActive=1 AND (COALESCE(PlayCount,0)>0 OR COALESCE(LastPlayedOrder,0)>0);";
    auto statement = m_connection.prepare(sql.c_str());
    if (!statement) {
        return result;
    }
    constexpr int kFirstExtra = track_rows::kTrackColumnCount;
    while (statement.value().step().valueOr(false)) {
        PlaybackStat stat;
        stat.track = track_rows::trackFromStatement(statement.value());
        // These three follow the track columns, so their indices move with it.
        stat.sourceKey = statement.value().columnText(kFirstExtra);
        stat.playCount = static_cast<quint32>(std::max(0, columnInt(statement.value(), kFirstExtra + 1)));
        stat.lastPlayedOrder = static_cast<quint64>(std::max<qint64>(0, statement.value().columnInt64(kFirstExtra + 2)));
        result.append(std::move(stat));
    }
    return result;
}

} // namespace lmp::core::db
