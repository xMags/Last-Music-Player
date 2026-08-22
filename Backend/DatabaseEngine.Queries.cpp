#include "pch.h"
#include "Backend/DatabaseEngine.h"
#include "Backend/DatabaseEngine.Internal.h"
#include "Backend/LibraryGroupingSql.h"

#include <algorithm>
#include <string>
#include <utility>

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
            "SELECT Id, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteId, LastPositionSeconds "
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
            "SELECT Id, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteId, LastPositionSeconds "
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
            "DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteId, LastPositionSeconds, SourceKey, LastPlayed "
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
            // Selected after kTrackColumns' fields, so these move with that list.
            candidate.SourceKey = ColumnText(stmt.value, 17);
            candidate.LastPlayedUtc = ColumnText(stmt.value, 18);
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
            "SELECT Id, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteId, LastPositionSeconds "
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

        // The curated-collection branches below honour SearchText and Sort the
        // same way the flat branch at the bottom does. They used to ignore both
        // and always return the curated order, which silently discarded any
        // find or sort a caller asked a playlist or album collection for.
        auto collectionSearchPattern = EscapedLikePattern(query.SearchText);
        auto const collectionSearch = LibraryGroupingSql::CollectionSearchClause(query.SearchText, 3);
        auto const collectionLimitIndex = query.SearchText.empty() ? 3 : 4;
        auto bindCollection = [&](sqlite3_stmt* stmt)
        {
            BindText(stmt, 1, query.GroupKey);
            sqlite3_bind_int(stmt, 2, query.ActiveOnly ? 1 : 0);
            if (!query.SearchText.empty())
            {
                BindText(stmt, 3, collectionSearchPattern);
            }
        };
        auto appendCollectionLimit = [&](std::string& sql)
        {
            if (limit <= 0)
            {
                return;
            }
            sql += " LIMIT ?" + std::to_string(collectionLimitIndex);
            sql += " OFFSET ?" + std::to_string(collectionLimitIndex + 1);
        };

        if (groupKind == L"album-collection")
        {
            auto countSql = std::string(
                "SELECT COUNT(*), COALESCE(SUM(t.DurationSeconds),0) "
                "FROM AlbumTracks at "
                "JOIN Albums a ON a.Id=at.AlbumId "
                "JOIN Tracks t ON t.Id=at.TrackId "
                "WHERE a.AlbumKey=?1 AND (?2=0 OR t.IsActive=1)") + collectionSearch;
            auto pageSql = std::string("SELECT ") + kJoinedTrackColumns +
                " FROM AlbumTracks at "
                "JOIN Albums a ON a.Id=at.AlbumId "
                "JOIN Tracks t ON t.Id=at.TrackId "
                "WHERE a.AlbumKey=?1 AND (?2=0 OR t.IsActive=1)" + collectionSearch + " " +
                LibraryGroupingSql::CollectionOrderClause(query.Sort, "at.TrackOrder");
            appendCollectionLimit(pageSql);

            readCount(countSql, bindCollection);
            readTracks(pageSql, [&](sqlite3_stmt* stmt)
            {
                bindCollection(stmt);
                bindLimitOffset(stmt, collectionLimitIndex);
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

            // This playlist is served from memory rather than SQL, so the find
            // and sort the other collection branches push into the query have
            // to be applied here by hand to keep the three paths consistent.
            if (!query.SearchText.empty())
            {
                auto const needle = ToLowerInvariant(query.SearchText);
                auto const matches = [&needle](TrackInfo const& track)
                {
                    auto const contains = [&needle](winrt::hstring const& field)
                    {
                        return ToLowerInvariant(std::wstring{ field.c_str() }).find(needle)
                            != std::wstring::npos;
                    };
                    return contains(track.Title()) || contains(track.Artist()) || contains(track.Album());
                };
                std::erase_if(tracks, [&matches](TrackInfo const& track) { return !matches(track); });
            }

            auto const sort = ToLowerInvariant(query.Sort);
            auto const byTitle = [](TrackInfo const& left, TrackInfo const& right)
            {
                return ToLowerInvariant(std::wstring{ left.Title().c_str() })
                    < ToLowerInvariant(std::wstring{ right.Title().c_str() });
            };
            if (sort == L"title")
            {
                std::stable_sort(tracks.begin(), tracks.end(), byTitle);
            }
            else if (sort == L"artist")
            {
                std::stable_sort(tracks.begin(), tracks.end(),
                    [&byTitle](TrackInfo const& left, TrackInfo const& right)
                    {
                        auto const leftArtist = ToLowerInvariant(std::wstring{ left.Artist().c_str() });
                        auto const rightArtist = ToLowerInvariant(std::wstring{ right.Artist().c_str() });
                        if (leftArtist != rightArtist)
                        {
                            return leftArtist < rightArtist;
                        }
                        return byTitle(left, right);
                    });
            }
            else if (sort == L"duration")
            {
                std::stable_sort(tracks.begin(), tracks.end(),
                    [&byTitle](TrackInfo const& left, TrackInfo const& right)
                    {
                        if (left.DurationSeconds() != right.DurationSeconds())
                        {
                            return left.DurationSeconds() > right.DurationSeconds();
                        }
                        return byTitle(left, right);
                    });
            }
            else if (sort == L"dateadded")
            {
                std::stable_sort(tracks.begin(), tracks.end(),
                    [&byTitle](TrackInfo const& left, TrackInfo const& right)
                    {
                        if (left.DateAddedSortKey() != right.DateAddedSortKey())
                        {
                            return left.DateAddedSortKey() > right.DateAddedSortKey();
                        }
                        return byTitle(left, right);
                    });
            }

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
            auto countSql = std::string(
                "SELECT COUNT(*), COALESCE(SUM(t.DurationSeconds),0) "
                "FROM PlaylistTracks pt "
                "JOIN Playlists p ON p.Id=pt.PlaylistId "
                "JOIN Tracks t ON t.Id=pt.TrackId "
                "WHERE p.PlaylistKey=?1 AND (?2=0 OR t.IsActive=1)") + collectionSearch;
            auto pageSql = std::string("SELECT ") + kJoinedTrackColumns +
                " FROM PlaylistTracks pt "
                "JOIN Playlists p ON p.Id=pt.PlaylistId "
                "JOIN Tracks t ON t.Id=pt.TrackId "
                "WHERE p.PlaylistKey=?1 AND (?2=0 OR t.IsActive=1)" + collectionSearch + " " +
                LibraryGroupingSql::CollectionOrderClause(query.Sort, "pt.TrackOrder");
            appendCollectionLimit(pageSql);

            readCount(countSql, bindCollection);
            readTracks(pageSql, [&](sqlite3_stmt* stmt)
            {
                bindCollection(stmt);
                bindLimitOffset(stmt, collectionLimitIndex);
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
            "SELECT Id, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteId, LastPositionSeconds "
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

        auto sql = LibraryGroupingSql::EligibleTracksCte(
            LibraryGroupingSql::GroupKind::Album)
            + "SELECT GroupTitle,"
            "MIN(CASE WHEN COALESCE(TRIM(Artist),'')='' THEN 'Unknown Artist' ELSE TRIM(Artist) END),"
            "MAX(COALESCE(ArtworkUrl,'')),"
            "COUNT(*), SUM(CASE WHEN SourceKind='remote' THEN 1 ELSE 0 END) "
            "FROM EligibleLibraryTracks WHERE GroupTitle<>'' AND Id NOT IN ("
            "SELECT at.TrackId FROM AlbumTracks at JOIN Albums a ON a.Id=at.AlbumId "
            "WHERE a.SourceKind<>'manual') "
            "GROUP BY GroupTitle COLLATE NOCASE "
            "ORDER BY COUNT(*) DESC, GroupTitle COLLATE NOCASE ASC;";

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

        Statement stmt{ m_db, sql.c_str() };
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
            "(SELECT COUNT(*) FROM PlaylistTracks WHERE PlaylistId=Playlists.Id), "
            "COALESCE(NULLIF(UpdatedAt,0),CreatedAt,0) "
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
            group.DateAddedSortKey(sqlite3_column_double(stmt.value, 10));
            groups.push_back(group);
        }

        Statement accountStmt{ m_db,
            "SELECT p.PlaylistId, p.Name, p.Description, p.SourceUrl, "
            "(SELECT COUNT(*) FROM AccountPlaylistTracks pt WHERE pt.AccountId=p.AccountId AND pt.PlaylistId=p.PlaylistId), "
            "COALESCE((SELECT t.ArtworkUrl FROM AccountPlaylistTracks pt JOIN AccountTracks t ON t.AccountId=pt.AccountId AND t.RemoteId=pt.RemoteId "
            "WHERE pt.AccountId=p.AccountId AND pt.PlaylistId=p.PlaylistId AND COALESCE(t.ArtworkUrl,'')<>'' "
            "ORDER BY pt.TrackOrder LIMIT 1),''), "
            "COALESCE(CAST(strftime('%s', p.UpdatedAtUtc) AS INTEGER),0) "
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
                group.DateAddedSortKey(sqlite3_column_double(accountStmt.value, 6));
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

        auto sql = LibraryGroupingSql::EligibleTracksCte(
            LibraryGroupingSql::GroupKind::Artist)
            + "SELECT GroupTitle, COUNT(*), "
            "COUNT(DISTINCT CASE WHEN COALESCE(TRIM(Album),'')='' "
            "OR LOWER(TRIM(Album))='unknown album' THEN NULL "
            "ELSE LOWER(TRIM(Album)) END),"
            "MAX(COALESCE(ArtworkUrl,'')),"
            "SUM(CASE WHEN SourceKind='remote' THEN 1 ELSE 0 END) "
            "FROM EligibleLibraryTracks WHERE GroupTitle<>'' "
            "GROUP BY GroupTitle COLLATE NOCASE "
            "ORDER BY COUNT(*) DESC, GroupTitle COLLATE NOCASE ASC;";

        Statement stmt{ m_db, sql.c_str() };
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

        auto sql = LibraryGroupingSql::EligibleTracksCte(
            LibraryGroupingSql::GroupKind::Genre)
            + "SELECT GroupTitle, COUNT(*), "
            "SUM(CASE WHEN SourceKind='remote' THEN 1 ELSE 0 END) "
            "FROM EligibleLibraryTracks WHERE GroupTitle<>'' "
            "GROUP BY GroupTitle COLLATE NOCASE "
            "ORDER BY COUNT(*) DESC, GroupTitle COLLATE NOCASE ASC;";

        Statement stmt{ m_db, sql.c_str() };
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

    std::vector<TrackInfo> DatabaseEngine::LoadTracksForGroup(
        std::wstring const& groupKind,
        std::wstring const& groupKey) const
    {
        if (groupKey.empty())
        {
            return {};
        }

        auto normalizedKind = ToLowerInvariant(groupKind);
        if (normalizedKind != L"album"
            && normalizedKind != L"artist"
            && normalizedKind != L"genre"
            && normalizedKind != L"album-collection"
            && normalizedKind != L"playlist")
        {
            return {};
        }

        TrackQuery query;
        query.GroupKind = std::move(normalizedKind);
        query.GroupKey = groupKey;
        query.IncludeRemote = true;
        query.ActiveOnly = true;
        return LoadTracksForQuery(query);
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
            // These three follow kTrackColumns, so their indices move with it.
            stat.SourceKey = ColumnText(stmt.value, 17);
            stat.PlayCount = static_cast<uint32_t>((std::max)(0, sqlite3_column_int(stmt.value, 18)));
            stat.LastPlayedOrder = static_cast<uint64_t>((std::max)(sqlite3_int64{ 0 }, sqlite3_column_int64(stmt.value, 19)));
            result.push_back(std::move(stat));
        }
        return result;
    }
}
