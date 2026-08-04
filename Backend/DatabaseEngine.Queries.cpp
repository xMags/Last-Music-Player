#include "pch.h"
#include "Backend/DatabaseEngine.h"
#include "Backend/DatabaseEngine.Internal.h"

#include <algorithm>
#include <string>

namespace LastMusicPlayer::Backend
{
    using namespace DatabaseDetail;

    namespace
    {
        std::wstring EscapedLikePattern(std::wstring const& text)
        {
            std::wstring pattern;
            pattern.reserve(text.size() + 2);
            pattern.push_back(L'%');
            for (auto const character : text)
            {
                if (character == L'!' || character == L'%' || character == L'_')
                {
                    pattern.push_back(L'!');
                }
                pattern.push_back(character);
            }
            pattern.push_back(L'%');
            return pattern;
        }
    }

    std::vector<TrackInfo> DatabaseEngine::LoadTracks(bool includeRemote, bool activeOnly) const
    {
        std::scoped_lock lock{ m_mutex };
        std::vector<TrackInfo> tracks;
        if (!m_db)
        {
            return tracks;
        }

        static constexpr char kSql[] =
            "SELECT Id, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteId "
            "FROM EffectiveTracks "
            "WHERE (?1=1 OR SourceKind='local') AND (?2=0 OR IsActive=1) "
            "ORDER BY DateAddedSortKey DESC, Title COLLATE NOCASE ASC;";

        Statement stmt{ m_db, kSql };
        if (!stmt) return tracks;
        sqlite3_bind_int(stmt.value, 1, includeRemote ? 1 : 0);
        sqlite3_bind_int(stmt.value, 2, activeOnly ? 1 : 0);

        int index = 1;
        while (sqlite3_step(stmt.value) == SQLITE_ROW)
        {
            auto track = TrackFromStatement(stmt.value);
            track.Index(index++);
            tracks.push_back(track);
        }
        return tracks;
    }

    std::vector<TrackInfo> DatabaseEngine::LoadHistoryTracks(bool includeRemote, bool activeOnly) const
    {
        std::scoped_lock lock{ m_mutex };
        std::vector<TrackInfo> tracks;
        if (!m_db)
        {
            return tracks;
        }

        static constexpr char kSql[] =
            "SELECT Id, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteId "
            "FROM EffectiveTracks "
            "WHERE (?1=1 OR SourceKind='local') AND (?2=0 OR IsActive=1) AND COALESCE(LastPlayedOrder,0)>0 "
            "ORDER BY LastPlayedOrder DESC, Title COLLATE NOCASE ASC;";

        Statement stmt{ m_db, kSql };
        if (!stmt) return tracks;
        sqlite3_bind_int(stmt.value, 1, includeRemote ? 1 : 0);
        sqlite3_bind_int(stmt.value, 2, activeOnly ? 1 : 0);

        int index = 1;
        while (sqlite3_step(stmt.value) == SQLITE_ROW)
        {
            auto track = TrackFromStatement(stmt.value);
            track.Index(index++);
            tracks.push_back(track);
        }
        return tracks;
    }
    std::vector<LegacyHistoryImportCandidate> DatabaseEngine::LoadLegacyRemoteHistoryImportCandidates() const
    {
        std::scoped_lock lock{ m_mutex };
        std::vector<LegacyHistoryImportCandidate> candidates;
        if (!m_db)
        {
            return candidates;
        }

        static constexpr char kSql[] =
            "SELECT Id, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, "
            "DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteId, SourceKey, LastPlayed "
            "FROM Tracks WHERE SourceKind='remote' AND COALESCE(PlayCount,0)>0 AND COALESCE(LastPlayedExact,0)=1 "
            "AND COALESCE(SourceUrl,'')<>'' AND COALESCE(LastPlayed,'')<>'' "
            "ORDER BY LastPlayed DESC, Id DESC;";
        Statement stmt{ m_db, kSql };
        if (!stmt)
        {
            return candidates;
        }

        while (sqlite3_step(stmt.value) == SQLITE_ROW)
        {
            LegacyHistoryImportCandidate candidate;
            candidate.Track = TrackFromStatement(stmt.value);
            candidate.SourceKey = ColumnText(stmt.value, 16);
            candidate.LastPlayedUtc = ColumnText(stmt.value, 17);
            if (candidate.Track && !candidate.SourceKey.empty() && !candidate.LastPlayedUtc.empty())
            {
                candidates.push_back(std::move(candidate));
            }
        }
        return candidates;
    }


    std::vector<TrackInfo> DatabaseEngine::LoadMostPlayedTracks(bool includeRemote, bool activeOnly) const
    {
        std::scoped_lock lock{ m_mutex };
        std::vector<TrackInfo> tracks;
        if (!m_db)
        {
            return tracks;
        }

        static constexpr char kSql[] =
            "SELECT Id, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteId "
            "FROM EffectiveTracks "
            "WHERE (?1=1 OR SourceKind='local') AND (?2=0 OR IsActive=1) AND COALESCE(PlayCount,0)>0 "
            "ORDER BY PlayCount DESC, LastPlayedOrder DESC, Title COLLATE NOCASE ASC;";

        Statement stmt{ m_db, kSql };
        if (!stmt) return tracks;
        sqlite3_bind_int(stmt.value, 1, includeRemote ? 1 : 0);
        sqlite3_bind_int(stmt.value, 2, activeOnly ? 1 : 0);

        int index = 1;
        while (sqlite3_step(stmt.value) == SQLITE_ROW)
        {
            auto track = TrackFromStatement(stmt.value);
            track.Index(index++);
            tracks.push_back(track);
        }
        return tracks;
    }

    TrackPage DatabaseEngine::LoadTrackPage(TrackQuery const& query) const
    {
        std::scoped_lock lock{ m_mutex };
        TrackPage page;
        if (!m_db)
        {
            return page;
        }

        auto groupKind = ToLowerInvariant(query.GroupKind);
        auto offset = (std::max)(0, query.Offset);
        auto limit = query.Limit;

        auto bindLimitOffset = [&](sqlite3_stmt* stmt, int firstIndex)
        {
            if (limit > 0)
            {
                sqlite3_bind_int(stmt, firstIndex, limit);
                sqlite3_bind_int(stmt, firstIndex + 1, offset);
            }
        };

        auto readCount = [&](std::string const& sql, auto binder)
        {
            Statement stmt{ m_db, sql.c_str() };
            if (!stmt)
            {
                return;
            }
            binder(stmt.value);
            if (sqlite3_step(stmt.value) == SQLITE_ROW)
            {
                page.TotalCount = sqlite3_column_int(stmt.value, 0);
                page.TotalSeconds = sqlite3_column_double(stmt.value, 1);
            }
        };

        auto readTracks = [&](std::string const& sql, auto binder)
        {
            Statement stmt{ m_db, sql.c_str() };
            if (!stmt)
            {
                return;
            }
            binder(stmt.value);
            int index = offset + 1;
            while (sqlite3_step(stmt.value) == SQLITE_ROW)
            {
                auto track = TrackFromStatement(stmt.value);
                track.Index(index++);
                page.Tracks.push_back(track);
            }
        };

        if (groupKind == L"album-collection")
        {
            auto countSql =
                "SELECT COUNT(*), COALESCE(SUM(t.DurationSeconds),0) "
                "FROM AlbumTracks at "
                "JOIN Albums a ON a.Id=at.AlbumId "
                "JOIN Tracks t ON t.Id=at.TrackId "
                "WHERE a.AlbumKey=?1 AND (?2=0 OR t.IsActive=1)";
            auto pageSql = std::string("SELECT ") + kJoinedTrackColumns +
                " FROM AlbumTracks at "
                "JOIN Albums a ON a.Id=at.AlbumId "
                "JOIN Tracks t ON t.Id=at.TrackId "
                "WHERE a.AlbumKey=?1 AND (?2=0 OR t.IsActive=1) "
                "ORDER BY at.TrackOrder ASC, t.Title COLLATE NOCASE ASC";
            if (limit > 0)
            {
                pageSql += " LIMIT ?3 OFFSET ?4";
            }

            auto binder = [&](sqlite3_stmt* stmt)
            {
                BindText(stmt, 1, query.GroupKey);
                sqlite3_bind_int(stmt, 2, query.ActiveOnly ? 1 : 0);
            };
            readCount(countSql, binder);
            readTracks(pageSql, [&](sqlite3_stmt* stmt)
            {
                binder(stmt);
                bindLimitOffset(stmt, 3);
            });
            return page;
        }

        static constexpr wchar_t kAccountPlaylistPrefix[] = L"account-playlist|";
        if (groupKind == L"playlist" && query.GroupKey.starts_with(kAccountPlaylistPrefix))
        {
            if (query.AccountOwnerId.empty())
            {
                return page;
            }
            auto playlistId = query.GroupKey.substr(std::size(kAccountPlaylistPrefix) - 1);
            auto tracks = LoadAccountPlaylistTracks(query.AccountOwnerId, playlistId);
            page.TotalCount = static_cast<int>(tracks.size());
            for (auto const& track : tracks)
            {
                page.TotalSeconds += track.DurationSeconds();
            }

            auto begin = static_cast<size_t>((std::min)(offset, page.TotalCount));
            auto end = tracks.size();
            if (limit > 0)
            {
                end = (std::min)(end, begin + static_cast<size_t>(limit));
            }
            page.Tracks.assign(tracks.begin() + begin, tracks.begin() + end);
            for (size_t index = 0; index < page.Tracks.size(); ++index)
            {
                page.Tracks[index].Index(static_cast<int32_t>(begin + index + 1));
            }
            return page;
        }
        if (groupKind == L"playlist")
        {
            auto countSql =
                "SELECT COUNT(*), COALESCE(SUM(t.DurationSeconds),0) "
                "FROM PlaylistTracks pt "
                "JOIN Playlists p ON p.Id=pt.PlaylistId "
                "JOIN Tracks t ON t.Id=pt.TrackId "
                "WHERE p.PlaylistKey=?1 AND (?2=0 OR t.IsActive=1)";
            auto pageSql = std::string("SELECT ") + kJoinedTrackColumns +
                " FROM PlaylistTracks pt "
                "JOIN Playlists p ON p.Id=pt.PlaylistId "
                "JOIN Tracks t ON t.Id=pt.TrackId "
                "WHERE p.PlaylistKey=?1 AND (?2=0 OR t.IsActive=1) "
                "ORDER BY pt.TrackOrder ASC, t.Title COLLATE NOCASE ASC";
            if (limit > 0)
            {
                pageSql += " LIMIT ?3 OFFSET ?4";
            }

            auto binder = [&](sqlite3_stmt* stmt)
            {
                BindText(stmt, 1, query.GroupKey);
                sqlite3_bind_int(stmt, 2, query.ActiveOnly ? 1 : 0);
            };
            readCount(countSql, binder);
            readTracks(pageSql, [&](sqlite3_stmt* stmt)
            {
                binder(stmt);
                bindLimitOffset(stmt, 3);
            });
            return page;
        }

        std::string where =
            "FROM EffectiveTracks "
            "WHERE (?1=1 OR SourceKind='local') AND (?2=0 OR IsActive=1)";
        where += TrackFilterClause(query.Filter);
        auto groupFilter = GroupFilterClause(query.GroupKind);
        if (query.Scope == L"Account")
        {
            where += " AND Provider='account'";
        }
        else if (query.Scope == L"OnThisPc")
        {
            where += " AND Provider<>'account'";
        }
        where += groupFilter;

        auto nextParameter = groupFilter.empty() ? 3 : 4;
        auto searchParameter = 0;
        auto searchPattern = std::wstring{};
        if (!query.SearchText.empty())
        {
            searchParameter = nextParameter++;
            searchPattern = EscapedLikePattern(query.SearchText);
            auto parameter = std::string("?") + std::to_string(searchParameter);
            where += " AND (COALESCE(Title,'') LIKE " + parameter + " ESCAPE '!'";
            where += " OR COALESCE(Artist,'') LIKE " + parameter + " ESCAPE '!'";
            where += " OR COALESCE(Album,'') LIKE " + parameter + " ESCAPE '!')";
        }

        auto countSql = std::string("SELECT COUNT(*), COALESCE(SUM(DurationSeconds),0) ") + where;
        auto pageSql = std::string("SELECT ") + kTrackColumns + " " + where + " " + TrackOrderClause(query.Filter, query.Sort);
        auto limitIndex = nextParameter;
        if (limit > 0)
        {
            pageSql += " LIMIT ?";
            pageSql += std::to_string(limitIndex);
            pageSql += " OFFSET ?";
            pageSql += std::to_string(limitIndex + 1);
        }

        auto binder = [&](sqlite3_stmt* stmt)
        {
            sqlite3_bind_int(stmt, 1, query.IncludeRemote ? 1 : 0);
            sqlite3_bind_int(stmt, 2, query.ActiveOnly ? 1 : 0);
            if (!groupFilter.empty())
            {
                BindText(stmt, 3, query.GroupKey);
            }
            if (searchParameter > 0)
            {
                BindText(stmt, searchParameter, searchPattern);
            }
        };
        readCount(countSql, binder);
        readTracks(pageSql, [&](sqlite3_stmt* stmt)
        {
            binder(stmt);
            bindLimitOffset(stmt, limitIndex);
        });
        return page;
    }

    std::vector<TrackInfo> DatabaseEngine::LoadTracksForQuery(TrackQuery const& query) const
    {
        std::scoped_lock lock{ m_mutex };
        auto fullQuery = query;
        fullQuery.Offset = 0;
        fullQuery.Limit = 0;
        return LoadTrackPage(fullQuery).Tracks;
    }

    std::vector<TrackInfo> DatabaseEngine::LoadRecentlyAddedTracks(int limit) const
    {
        std::scoped_lock lock{ m_mutex };
        std::vector<TrackInfo> tracks;
        if (!m_db)
        {
            return tracks;
        }

        // Queried straight from the view, like the history and most-played
        // shelves either side of it, rather than through the paged library
        // query: that one carries scope and grouping rules meant for the songs
        // list, which filtered this shelf down to nothing for an account with
        // no local files.
        //
        // The non-zero test is the shelf's own rule and mirrors the web client,
        // which keeps a track only when it resolves an added time. A track that
        // was never added to the library, only passed through in a search
        // result, holds zero here and stays out.
        static constexpr char kSql[] =
            "SELECT Id, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteId "
            "FROM EffectiveTracks "
            "WHERE IsActive=1 AND COALESCE(DateAddedSortKey,0)<>0 "
            "ORDER BY DateAddedSortKey DESC, Title COLLATE NOCASE ASC "
            "LIMIT ?1;";

        Statement stmt{ m_db, kSql };
        if (!stmt) return tracks;
        sqlite3_bind_int(stmt.value, 1, limit > 0 ? limit : -1);

        int index = 1;
        while (sqlite3_step(stmt.value) == SQLITE_ROW)
        {
            auto track = TrackFromStatement(stmt.value);
            track.Index(index++);
            tracks.push_back(track);
        }
        return tracks;
    }

    std::vector<TrackInfo> DatabaseEngine::LoadAlbums() const
    {
        std::scoped_lock lock{ m_mutex };
        std::vector<TrackInfo> groups;
        if (!m_db) return groups;

        static constexpr char kSql[] =
            "SELECT "
            "CASE WHEN Album IS NULL OR Album='' THEN CASE WHEN SourceKind='remote' THEN 'Remote Singles' ELSE 'Unknown Album' END ELSE Album END AS GroupTitle,"
            "MIN(CASE WHEN Artist IS NULL OR Artist='' THEN 'Unknown Artist' ELSE Artist END),"
            "MAX(CASE WHEN ArtworkUrl IS NULL THEN '' ELSE ArtworkUrl END),"
            "COUNT(*), SUM(CASE WHEN SourceKind='remote' THEN 1 ELSE 0 END) "
            "FROM EffectiveTracks WHERE IsActive=1 AND Id NOT IN (SELECT at.TrackId FROM AlbumTracks at JOIN Albums a ON a.Id=at.AlbumId WHERE a.SourceKind<>'manual') GROUP BY GroupTitle ORDER BY GroupTitle COLLATE NOCASE ASC;";

        Statement collections{ m_db,
            "SELECT Id, AlbumKey, Title, Artist, ArtworkUrl, SourceKind, SourceUrl, SourceLabel, "
            "(SELECT COUNT(*) FROM AlbumTracks WHERE AlbumId=Albums.Id) "
            "FROM Albums ORDER BY UpdatedAt DESC, Title COLLATE NOCASE ASC;" };
        if (collections)
        {
            while (sqlite3_step(collections.value) == SQLITE_ROW)
            {
                TrackInfo group;
                group.CatalogId(sqlite3_column_int64(collections.value, 0));
                auto collectionSourceKind = ColumnText(collections.value, 5);
                auto artist = ColumnText(collections.value, 3);
                if (ToLowerInvariant(collectionSourceKind) == L"manual" && (artist.empty() || artist == L"Manual album"))
                {
                    artist = L"Your hand-picked songs live here.";
                }

                group.SourceUrl(winrt::hstring(ColumnText(collections.value, 1)));
                group.Title(winrt::hstring(ColumnText(collections.value, 2)));
                group.Artist(winrt::hstring(artist));
                ApplyArtworkUrl(group, winrt::hstring(ColumnText(collections.value, 4)));
                group.Provider(winrt::hstring(collectionSourceKind));
                group.SourceKind(L"album-collection");
                auto sourceLabel = ColumnText(collections.value, 7);
                group.SourceLabel(sourceLabel.empty() ? winrt::hstring{ L"Album" } : winrt::hstring(sourceLabel));
                group.TrackCount(sqlite3_column_int(collections.value, 8));
                groups.push_back(group);
            }
        }

        Statement stmt{ m_db, kSql };
        if (!stmt) return groups;
        while (sqlite3_step(stmt.value) == SQLITE_ROW)
        {
            TrackInfo group;
            auto count = sqlite3_column_int(stmt.value, 3);
            auto remoteCount = sqlite3_column_int(stmt.value, 4);
            group.Title(winrt::hstring(ColumnText(stmt.value, 0)));
            group.Artist(winrt::hstring(ColumnText(stmt.value, 1)));
            ApplyArtworkUrl(group, winrt::hstring(ColumnText(stmt.value, 2)));
            group.TrackCount(count);
            group.SourceKind(L"album");
            group.SourceUrl(group.Title());
            group.SourceLabel(remoteCount == 0 ? L"Local" : (remoteCount == count ? L"Remote" : L"Mixed"));
            groups.push_back(group);
        }
        return groups;
    }

    std::vector<TrackInfo> DatabaseEngine::LoadPlaylists() const
    {
        std::scoped_lock lock{ m_mutex };
        std::vector<TrackInfo> groups;
        if (!m_db) return groups;

        Statement stmt{ m_db,
            "SELECT Id, PlaylistKey, Title, Description, ArtworkUrl, SourceKind, Provider, SourceUrl, SourceLabel, "
            "(SELECT COUNT(*) FROM PlaylistTracks WHERE PlaylistId=Playlists.Id) "
            "FROM Playlists ORDER BY UpdatedAt DESC, Title COLLATE NOCASE ASC;" };
        if (!stmt) return groups;

        while (sqlite3_step(stmt.value) == SQLITE_ROW)
        {
            TrackInfo group;
            auto count = sqlite3_column_int(stmt.value, 9);
            auto description = ColumnText(stmt.value, 3);
            auto sourceLabel = ColumnText(stmt.value, 8);
            group.CatalogId(sqlite3_column_int64(stmt.value, 0));
            group.SourceUrl(winrt::hstring(ColumnText(stmt.value, 1)));
            group.Title(winrt::hstring(ColumnText(stmt.value, 2)));
            group.Artist(winrt::hstring(description.empty()
                ? (std::to_wstring(count) + (count == 1 ? L" song" : L" songs"))
                : description));
            ApplyArtworkUrl(group, winrt::hstring(ColumnText(stmt.value, 4)));
            group.Provider(winrt::hstring(ColumnText(stmt.value, 6)));
            group.SourceKind(L"playlist");
            group.SourceLabel(L"On this PC");
            group.TrackCount(count);
            groups.push_back(group);
        }

        Statement accountStmt{ m_db,
            "SELECT p.PlaylistId, p.Name, p.Description, p.SourceUrl, "
            "(SELECT COUNT(*) FROM AccountPlaylistTracks pt WHERE pt.AccountId=p.AccountId AND pt.PlaylistId=p.PlaylistId), "
            "COALESCE((SELECT t.ArtworkUrl FROM AccountPlaylistTracks pt JOIN AccountTracks t ON t.AccountId=pt.AccountId AND t.RemoteId=pt.RemoteId "
            "WHERE pt.AccountId=p.AccountId AND pt.PlaylistId=p.PlaylistId AND COALESCE(t.ArtworkUrl,'')<>'' "
            "ORDER BY pt.TrackOrder LIMIT 1),'') "
            "FROM AccountPlaylists p JOIN ActiveAccountContext c ON c.SingletonId=1 AND c.AccountId=p.AccountId "
            "ORDER BY p.UpdatedAtUtc DESC, p.Name COLLATE NOCASE ASC;" };
        if (accountStmt)
        {
            while (sqlite3_step(accountStmt.value) == SQLITE_ROW)
            {
                TrackInfo group;
                auto playlistId = ColumnText(accountStmt.value, 0);
                auto count = sqlite3_column_int(accountStmt.value, 4);
                auto description = ColumnText(accountStmt.value, 2);
                group.RemoteId(winrt::hstring(playlistId));
                group.SourceUrl(winrt::hstring(L"account-playlist|" + playlistId));
                group.Title(winrt::hstring(ColumnText(accountStmt.value, 1)));
                group.Artist(winrt::hstring(description.empty()
                    ? (std::to_wstring(count) + (count == 1 ? L" song" : L" songs"))
                    : description));
                ApplyArtworkUrl(group, winrt::hstring(ColumnText(accountStmt.value, 5)));
                group.Provider(L"account");
                group.SourceKind(L"playlist");
                group.SourceLabel(L"Synced");
                group.TrackCount(count);
                groups.push_back(group);
            }
        }
        return groups;
    }

    std::vector<TrackInfo> DatabaseEngine::LoadRecentPlaylists(int limit) const
    {
        std::scoped_lock lock{ m_mutex };
        auto groups = LoadPlaylists();
        std::stable_sort(groups.begin(), groups.end(), [](TrackInfo const& left, TrackInfo const& right)
        {
            auto leftAccount = left.Provider() == L"account";
            auto rightAccount = right.Provider() == L"account";
            if (leftAccount != rightAccount)
            {
                return leftAccount;
            }
            return left.Title() < right.Title();
        });
        if (limit <= 0)
        {
            return {};
        }
        if (groups.size() > static_cast<size_t>(limit))
        {
            groups.resize(static_cast<size_t>(limit));
        }
        return groups;
    }

    std::vector<TrackInfo> DatabaseEngine::LoadArtists() const
    {
        std::scoped_lock lock{ m_mutex };
        std::vector<TrackInfo> groups;
        if (!m_db) return groups;

        static constexpr char kSql[] =
            "SELECT CASE WHEN Artist IS NULL OR Artist='' THEN 'Unknown Artist' ELSE Artist END AS GroupTitle,"
            "COUNT(*), COUNT(DISTINCT CASE WHEN Album IS NULL OR Album='' THEN 'Unknown Album' ELSE Album END),"
            "MAX(CASE WHEN ArtworkUrl IS NULL THEN '' ELSE ArtworkUrl END),"
            "SUM(CASE WHEN SourceKind='remote' THEN 1 ELSE 0 END) "
            "FROM EffectiveTracks WHERE IsActive=1 GROUP BY GroupTitle ORDER BY GroupTitle COLLATE NOCASE ASC;";

        Statement stmt{ m_db, kSql };
        if (!stmt) return groups;
        while (sqlite3_step(stmt.value) == SQLITE_ROW)
        {
            TrackInfo group;
            auto count = sqlite3_column_int(stmt.value, 1);
            auto albums = sqlite3_column_int(stmt.value, 2);
            auto remoteCount = sqlite3_column_int(stmt.value, 4);
            group.Title(winrt::hstring(ColumnText(stmt.value, 0)));
            group.Artist(winrt::hstring(std::to_wstring(count) + L" songs - " + std::to_wstring(albums) + L" albums"));
            ApplyArtworkUrl(group, winrt::hstring(ColumnText(stmt.value, 3)));
            group.TrackCount(count);
            group.SourceKind(L"artist");
            group.SourceUrl(group.Title());
            group.SourceLabel(remoteCount == 0 ? L"Local" : (remoteCount == count ? L"Remote" : L"Mixed"));
            groups.push_back(group);
        }
        return groups;
    }

    std::vector<TrackInfo> DatabaseEngine::LoadGenres() const
    {
        std::scoped_lock lock{ m_mutex };
        std::vector<TrackInfo> groups;
        if (!m_db) return groups;

        static constexpr char kSql[] =
            "SELECT CASE WHEN Genre IS NULL OR Genre='' THEN 'Unknown Genre' ELSE Genre END AS GroupTitle,"
            "COUNT(*), SUM(CASE WHEN SourceKind='remote' THEN 1 ELSE 0 END) "
            "FROM EffectiveTracks WHERE IsActive=1 GROUP BY GroupTitle ORDER BY GroupTitle COLLATE NOCASE ASC;";

        Statement stmt{ m_db, kSql };
        if (!stmt) return groups;
        while (sqlite3_step(stmt.value) == SQLITE_ROW)
        {
            TrackInfo group;
            auto count = sqlite3_column_int(stmt.value, 1);
            auto remoteCount = sqlite3_column_int(stmt.value, 2);
            group.Title(winrt::hstring(ColumnText(stmt.value, 0)));
            group.Artist(winrt::hstring(std::to_wstring(count) + (count == 1 ? L" song" : L" songs")));
            group.TrackCount(count);
            group.SourceKind(L"genre");
            group.SourceUrl(group.Title());
            group.SourceLabel(remoteCount == 0 ? L"Local" : (remoteCount == count ? L"Remote" : L"Mixed"));
            groups.push_back(group);
        }
        return groups;
    }

    std::vector<std::wstring> DatabaseEngine::LoadTopGenres(int limit) const
    {
        std::scoped_lock lock{ m_mutex };
        std::vector<std::wstring> genres;
        if (!m_db || limit <= 0)
        {
            return genres;
        }

        // Rank by listening first (SUM of per-track PlayCount), then by genre
        // size so a brand-new library with no plays still yields the user's
        // biggest genres rather than nothing. Untagged tracks (empty Genre) are
        // excluded via HAVING so a Daily Mix is never "Unknown Genre".
        // Remote metadata can contain broad media categories rather than musical
        // genres. Exclude those generic categories from mix labels; libraries
        // without useful genre tags fall back to artist clustering.
        static constexpr char kSql[] =
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

        Statement stmt{ m_db, kSql };
        if (!stmt) return genres;
        sqlite3_bind_int(stmt.value, 1, limit);
        while (sqlite3_step(stmt.value) == SQLITE_ROW)
        {
            auto name = ColumnText(stmt.value, 0);
            if (!name.empty())
            {
                genres.push_back(std::move(name));
            }
        }
        return genres;
    }

    std::vector<TrackInfo> DatabaseEngine::LoadTracksForGroup(std::wstring const& groupKind, std::wstring const& groupKey) const
    {
        std::scoped_lock lock{ m_mutex };
        std::vector<TrackInfo> tracks;
        if (!m_db || groupKey.empty())
        {
            return tracks;
        }

        if (groupKind == L"album-collection")
        {
            static constexpr char kCollectionSql[] =
                "SELECT t.Id, t.SourceKind, t.Provider, t.SourceUrl, t.FilePath, t.Title, t.Artist, t.Album, t.Genre, t.DurationSeconds, t.ArtworkUrl, t.DateAddedSortKey, t.DateAddedText, t.DurationText, t.IsLiked "
                "FROM AlbumTracks at "
                "JOIN Albums a ON a.Id=at.AlbumId "
                "JOIN Tracks t ON t.Id=at.TrackId "
                "WHERE a.AlbumKey=?1 AND t.IsActive=1 "
                "ORDER BY at.TrackOrder ASC, t.Title COLLATE NOCASE ASC;";

            Statement stmt{ m_db, kCollectionSql };
            if (!stmt) return tracks;
            BindText(stmt.value, 1, groupKey);
            int index = 1;
            while (sqlite3_step(stmt.value) == SQLITE_ROW)
            {
                auto track = TrackFromStatement(stmt.value);
                track.Index(index++);
                tracks.push_back(track);
            }
            return tracks;
        }

        if (groupKind == L"playlist")
        {
            static constexpr char kPlaylistSql[] =
                "SELECT t.Id, t.SourceKind, t.Provider, t.SourceUrl, t.FilePath, t.Title, t.Artist, t.Album, t.Genre, t.DurationSeconds, t.ArtworkUrl, t.DateAddedSortKey, t.DateAddedText, t.DurationText, t.IsLiked "
                "FROM PlaylistTracks pt "
                "JOIN Playlists p ON p.Id=pt.PlaylistId "
                "JOIN Tracks t ON t.Id=pt.TrackId "
                "WHERE p.PlaylistKey=?1 AND t.IsActive=1 "
                "ORDER BY pt.TrackOrder ASC, t.Title COLLATE NOCASE ASC;";

            Statement stmt{ m_db, kPlaylistSql };
            if (!stmt) return tracks;
            BindText(stmt.value, 1, groupKey);
            int index = 1;
            while (sqlite3_step(stmt.value) == SQLITE_ROW)
            {
                auto track = TrackFromStatement(stmt.value);
                track.Index(index++);
                tracks.push_back(track);
            }
            return tracks;
        }

        char const* sql =
            "SELECT Id, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteId "
            "FROM EffectiveTracks WHERE IsActive=1 AND "
            "CASE ?1 "
            "WHEN 'album' THEN CASE WHEN Album IS NULL OR Album='' THEN CASE WHEN SourceKind='remote' THEN 'Remote Singles' ELSE 'Unknown Album' END ELSE Album END "
            "WHEN 'artist' THEN CASE WHEN Artist IS NULL OR Artist='' THEN 'Unknown Artist' ELSE Artist END "
            "WHEN 'genre' THEN CASE WHEN Genre IS NULL OR Genre='' THEN 'Unknown Genre' ELSE Genre END "
            "ELSE '' END = ?2 "
            "ORDER BY CASE WHEN DateAddedSortKey IS NULL THEN 0 ELSE DateAddedSortKey END DESC, Title COLLATE NOCASE ASC;";

        Statement stmt{ m_db, sql };
        if (!stmt) return tracks;
        BindText(stmt.value, 1, groupKind);
        BindText(stmt.value, 2, groupKey);
        int index = 1;
        while (sqlite3_step(stmt.value) == SQLITE_ROW)
        {
            auto track = TrackFromStatement(stmt.value);
            track.Index(index++);
            tracks.push_back(track);
        }
        return tracks;
    }

    LibraryStats DatabaseEngine::GetLibraryStats() const
    {
        std::scoped_lock lock{ m_mutex };
        LibraryStats stats;
        if (!m_db)
        {
            return stats;
        }

        Statement totals{ m_db,
            "SELECT COUNT(*), "
            "(SELECT COUNT(*) FROM Albums) + COUNT(DISTINCT CASE WHEN Id NOT IN (SELECT at.TrackId FROM AlbumTracks at JOIN Albums a ON a.Id=at.AlbumId WHERE a.SourceKind<>'manual') THEN CASE WHEN Album IS NULL OR Album='' THEN CASE WHEN SourceKind='remote' THEN 'Remote Singles' ELSE 'Unknown Album' END ELSE Album END END), "
            "COUNT(DISTINCT CASE WHEN Artist IS NULL OR Artist='' THEN 'Unknown Artist' ELSE Artist END), "
            "COUNT(DISTINCT CASE WHEN Genre IS NULL OR Genre='' THEN 'Unknown Genre' ELSE Genre END), "
            "COALESCE(SUM(DurationSeconds),0) FROM EffectiveTracks WHERE IsActive=1;" };
        if (totals && sqlite3_step(totals.value) == SQLITE_ROW)
        {
            stats.SongCount = sqlite3_column_int(totals.value, 0);
            stats.AlbumCount = sqlite3_column_int(totals.value, 1);
            stats.ArtistCount = sqlite3_column_int(totals.value, 2);
            stats.GenreCount = sqlite3_column_int(totals.value, 3);
            stats.TotalSeconds = sqlite3_column_double(totals.value, 4);
        }
        return stats;
    }

    std::vector<PlaybackStatSnapshot> DatabaseEngine::LoadPlaybackStats() const
    {
        std::scoped_lock lock{ m_mutex };
        std::vector<PlaybackStatSnapshot> result;
        if (!m_db)
        {
            return result;
        }

        auto sql = std::string("SELECT ") + kTrackColumns +
            ", SourceKey, COALESCE(PlayCount,0), COALESCE(LastPlayedOrder,0) FROM EffectiveTracks "
            "WHERE IsActive=1 AND (COALESCE(PlayCount,0)>0 OR COALESCE(LastPlayedOrder,0)>0);";
        Statement stmt{ m_db, sql.c_str() };
        if (!stmt)
        {
            return result;
        }
        while (sqlite3_step(stmt.value) == SQLITE_ROW)
        {
            PlaybackStatSnapshot stat;
            stat.Track = TrackFromStatement(stmt.value);
            stat.SourceKey = ColumnText(stmt.value, 16);
            stat.PlayCount = static_cast<uint32_t>((std::max)(0, sqlite3_column_int(stmt.value, 17)));
            stat.LastPlayedOrder = static_cast<uint64_t>((std::max)(sqlite3_int64{ 0 }, sqlite3_column_int64(stmt.value, 18)));
            result.push_back(std::move(stat));
        }
        return result;
    }
}
