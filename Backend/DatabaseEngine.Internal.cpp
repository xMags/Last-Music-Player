#include "pch.h"
#include "Backend/DatabaseEngine.Internal.h"
#include "Backend/ProviderHelpers.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace LastMusicPlayer::Backend::DatabaseDetail
{
    std::filesystem::path AppDataDirectory()
    {
        wchar_t* localAppData{};
        size_t length{};
        if (_wdupenv_s(&localAppData, &length, L"LOCALAPPDATA") == 0 && localAppData && *localAppData)
        {
            auto path = std::filesystem::path{ localAppData } / L"Last Music Player";
            std::free(localAppData);
            return path;
        }
        std::free(localAppData);

        return std::filesystem::current_path() / L"Last Music Player";
    }

    std::string WideToUtf8(std::wstring const& wide)
    {
        if (wide.empty())
        {
            return {};
        }

        int requiredSize = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
        if (requiredSize <= 0)
        {
            return {};
        }

        std::string utf8(static_cast<size_t>(requiredSize), '\0');
        ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()), utf8.data(), requiredSize, nullptr, nullptr);
        return utf8;
    }

    std::wstring Utf8ToWide(char const* utf8)
    {
        if (utf8 == nullptr || *utf8 == '\0')
        {
            return {};
        }

        int requiredSize = ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
        if (requiredSize <= 1)
        {
            return {};
        }

        std::wstring wide(static_cast<size_t>(requiredSize - 1), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide.data(), requiredSize);
        return wide;
    }

    void BindText(sqlite3_stmt* stmt, int index, std::wstring const& value)
    {
        auto utf8 = WideToUtf8(value);
        sqlite3_bind_text(stmt, index, utf8.c_str(), -1, SQLITE_TRANSIENT);
    }

    std::wstring ColumnText(sqlite3_stmt* stmt, int index)
    {
        return Utf8ToWide(reinterpret_cast<char const*>(sqlite3_column_text(stmt, index)));
    }

    std::wstring ToLowerInvariant(std::wstring value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch)
        {
            return static_cast<wchar_t>(std::towlower(ch));
        });
        return value;
    }

    bool Exec(sqlite3* db, char const* sql)
    {
        char* error{};
        auto rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
        if (rc != SQLITE_OK)
        {
            if (error)
            {
                ::OutputDebugStringA(error);
                sqlite3_free(error);
            }
            return false;
        }
        return true;
    }

    void TryExec(sqlite3* db, char const* sql)
    {
        char* error{};
        auto rc = sqlite3_exec(db, sql, nullptr, nullptr, &error);
        if (rc != SQLITE_OK && error)
        {
            sqlite3_free(error);
        }
    }

    std::wstring TimestampKeyPart()
    {
        auto ticks = std::chrono::system_clock::now().time_since_epoch().count();
        return std::to_wstring(static_cast<long long>(ticks));
    }

    Statement::Statement(sqlite3* db, char const* sql)
    {
        if (sqlite3_prepare_v2(db, sql, -1, &value, nullptr) != SQLITE_OK)
        {
            value = nullptr;
        }
    }

    Statement::~Statement()
    {
        if (value)
        {
            sqlite3_finalize(value);
        }
    }

    Statement::operator bool() const
    {
        return value != nullptr;
    }

    bool TracksTableHasLegacyFilePathUnique(sqlite3* db)
    {
        Statement stmt{ db, "SELECT sql FROM sqlite_master WHERE type='table' AND name='Tracks';" };
        if (!stmt || sqlite3_step(stmt.value) != SQLITE_ROW)
        {
            return false;
        }

        auto schema = ColumnText(stmt.value, 0);
        return schema.find(L"FilePath TEXT UNIQUE") != std::wstring::npos;
    }

    bool RebuildTracksTableWithoutFilePathUnique(sqlite3* db)
    {
        static constexpr char kRebuildSql[] =
            "DROP TABLE IF EXISTS Tracks_rebuild;"
            "CREATE TABLE Tracks_rebuild ("
            "Id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "SourceKey TEXT,"
            "SourceKind TEXT DEFAULT 'local',"
            "Provider TEXT,"
            "SourceUrl TEXT,"
            "FilePath TEXT,"
            "Title TEXT,"
            "Artist TEXT,"
            "Album TEXT,"
            "Genre TEXT,"
            "DurationSeconds REAL DEFAULT 0,"
            "ArtworkUrl TEXT,"
            "DateAddedSortKey REAL DEFAULT 0,"
            "DateAddedText TEXT,"
            "DurationText TEXT,"
            "PlayCount INTEGER DEFAULT 0,"
            "LastPlayedOrder INTEGER DEFAULT 0,"
            "IsLiked INTEGER DEFAULT 0,"
            "IsActive INTEGER DEFAULT 1,"
            "UpdatedAt INTEGER DEFAULT 0,"
            "LastPlayed DATETIME,"
            "LastPlayedExact INTEGER DEFAULT 0,"
            "RemoteId TEXT);"
            "INSERT INTO Tracks_rebuild (Id, SourceKey, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, PlayCount, LastPlayedOrder, IsLiked, IsActive, UpdatedAt, LastPlayed, LastPlayedExact, RemoteId) "
            "SELECT Id, SourceKey, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, PlayCount, LastPlayedOrder, IsLiked, IsActive, UpdatedAt, LastPlayed, COALESCE(LastPlayedExact,0), RemoteId FROM Tracks;"
            "DROP TABLE Tracks;"
            "ALTER TABLE Tracks_rebuild RENAME TO Tracks;";

        if (!Exec(db, "BEGIN IMMEDIATE;"))
        {
            return false;
        }

        if (!Exec(db, kRebuildSql))
        {
            TryExec(db, "ROLLBACK;");
            return false;
        }

        if (!Exec(db, "COMMIT;"))
        {
            TryExec(db, "ROLLBACK;");
            return false;
        }

        return true;
    }

    bool NormalizeRemoteTrackSourceKeys(sqlite3* db)
    {
        // Behaviorally identical to the original 5-correlated-subquery
        // UPDATE, but the temp tables are now indexed and the per-group
        // aggregates are precomputed once into RemoteSourceKeyAgg instead
        // of being recomputed (with a nested lookup) for every keeper row
        // — turning an O(n^2)-ish migration into a few indexed passes.
        // The aggregate is computed BEFORE the dedupe DELETE, exactly as
        // before, so non-keeper rows still contribute their MAX values.
        static constexpr char kNormalizeSql[] =
            "DROP TABLE IF EXISTS temp.RemoteSourceKeyMap;"
            "DROP TABLE IF EXISTS temp.RemoteSourceKeyKeepers;"
            "DROP TABLE IF EXISTS temp.RemoteSourceKeyAgg;"
            "CREATE TEMP TABLE RemoteSourceKeyMap AS "
            "SELECT Id, 'remote|' || lower(COALESCE(NULLIF(Provider,''),'provider')) || '|' || SourceUrl AS TargetKey "
            "FROM Tracks "
            "WHERE SourceKind='remote' AND SourceUrl IS NOT NULL AND SourceUrl<>'';"
            "CREATE UNIQUE INDEX IX_RemoteSourceKeyMap_Id ON RemoteSourceKeyMap(Id);"
            "CREATE INDEX IX_RemoteSourceKeyMap_Key ON RemoteSourceKeyMap(TargetKey);"
            "CREATE TEMP TABLE RemoteSourceKeyKeepers AS "
            "SELECT TargetKey, MIN(Id) AS KeepId "
            "FROM RemoteSourceKeyMap "
            "GROUP BY TargetKey;"
            "CREATE UNIQUE INDEX IX_RemoteSourceKeyKeepers_Key ON RemoteSourceKeyKeepers(TargetKey);"
            "CREATE UNIQUE INDEX IX_RemoteSourceKeyKeepers_KeepId ON RemoteSourceKeyKeepers(KeepId);"
            "CREATE TEMP TABLE RemoteSourceKeyAgg AS "
            "SELECT m.TargetKey AS TargetKey,"
            "MAX(COALESCE(t.IsLiked,0)) AS IsLiked,"
            "MAX(COALESCE(t.PlayCount,0)) AS PlayCount,"
            "MAX(COALESCE(t.LastPlayedOrder,0)) AS LastPlayedOrder,"
            "MAX(COALESCE(t.UpdatedAt,0)) AS UpdatedAt,"
            "MAX(t.LastPlayed) AS LastPlayed "
            "FROM Tracks t JOIN RemoteSourceKeyMap m ON m.Id=t.Id "
            "GROUP BY m.TargetKey;"
            "CREATE UNIQUE INDEX IX_RemoteSourceKeyAgg_Key ON RemoteSourceKeyAgg(TargetKey);"
            "UPDATE Tracks SET "
            "(IsLiked, PlayCount, LastPlayedOrder, UpdatedAt, LastPlayed)="
            "(SELECT a.IsLiked, a.PlayCount, a.LastPlayedOrder, a.UpdatedAt, a.LastPlayed "
            "FROM RemoteSourceKeyKeepers k JOIN RemoteSourceKeyAgg a ON a.TargetKey=k.TargetKey "
            "WHERE k.KeepId=Tracks.Id) "
            "WHERE Id IN (SELECT KeepId FROM RemoteSourceKeyKeepers);"
            "DELETE FROM Tracks "
            "WHERE Id IN (SELECT Id FROM RemoteSourceKeyMap WHERE Id NOT IN (SELECT KeepId FROM RemoteSourceKeyKeepers));"
            "UPDATE Tracks SET SourceKey=(SELECT TargetKey FROM RemoteSourceKeyMap WHERE Id=Tracks.Id) "
            "WHERE Id IN (SELECT KeepId FROM RemoteSourceKeyKeepers) "
            "AND COALESCE(SourceKey,'')<>(SELECT TargetKey FROM RemoteSourceKeyMap WHERE Id=Tracks.Id);"
            "DROP TABLE IF EXISTS temp.RemoteSourceKeyMap;"
            "DROP TABLE IF EXISTS temp.RemoteSourceKeyKeepers;"
            "DROP TABLE IF EXISTS temp.RemoteSourceKeyAgg;";

        if (!Exec(db, "BEGIN IMMEDIATE;"))
        {
            return false;
        }

        if (!Exec(db, kNormalizeSql))
        {
            TryExec(db, "ROLLBACK;");
            return false;
        }

        if (!Exec(db, "COMMIT;"))
        {
            TryExec(db, "ROLLBACK;");
            return false;
        }

        return true;
    }

    std::wstring StoredFilePathFor(TrackInfo const& track, std::wstring const& sourceKind, std::wstring const& sourceKey)
    {
        (void)sourceKey;
        if (sourceKind == L"remote")
        {
            return {};
        }
        return std::wstring(RemoveLegacyProviderUrlCredential(track.FilePath()).c_str());
    }

    winrt::hstring SourceLabelFor(std::wstring const& sourceKind)
    {
        return sourceKind == L"remote" ? winrt::hstring{ L"Remote" } : winrt::hstring{ L"Local" };
    }

    void ApplyArtworkUrl(TrackInfo& track, winrt::hstring const& artworkUrl)
    {
        track.ArtworkUrl(artworkUrl);
    }

    TrackInfo TrackFromStatement(sqlite3_stmt* stmt)
    {
        TrackInfo track;
        track.CatalogId(sqlite3_column_int64(stmt, 0));
        auto sourceKind = ColumnText(stmt, 1);
        track.SourceKind(winrt::hstring(sourceKind));
        track.Provider(winrt::hstring(ColumnText(stmt, 2)));
        track.SourceUrl(winrt::hstring(ColumnText(stmt, 3)));
        track.FilePath(RemoveLegacyProviderUrlCredential(winrt::hstring(ColumnText(stmt, 4))));
        track.Title(winrt::hstring(ColumnText(stmt, 5)));
        track.Artist(winrt::hstring(ColumnText(stmt, 6)));
        track.Album(winrt::hstring(ColumnText(stmt, 7)));
        track.Genre(winrt::hstring(ColumnText(stmt, 8)));
        track.DurationSeconds(sqlite3_column_double(stmt, 9));
        ApplyArtworkUrl(track, RemoveLegacyProviderUrlCredential(winrt::hstring(ColumnText(stmt, 10))));
        track.DateAddedSortKey(sqlite3_column_double(stmt, 11));
        track.DateAdded(winrt::hstring(ColumnText(stmt, 12)));
        track.Duration(winrt::hstring(ColumnText(stmt, 13)));
        track.IsLiked(sqlite3_column_int(stmt, 14) != 0);
        track.SourceLabel(SourceLabelFor(sourceKind));
        if (sourceKind == L"remote" && track.FilePath().empty() && ColumnText(stmt, 2) != L"account")
        {
            track.FilePath(track.SourceUrl());
        }
        track.RemoteId(winrt::hstring(ColumnText(stmt, 15)));
        return track;
    }

    extern char const kTrackColumns[] =
        "Id, SourceKind, Provider, SourceUrl, FilePath, Title, Artist, Album, Genre, DurationSeconds, ArtworkUrl, DateAddedSortKey, DateAddedText, DurationText, IsLiked, RemoteId";

    extern char const kJoinedTrackColumns[] =
        "t.Id, t.SourceKind, t.Provider, t.SourceUrl, t.FilePath, t.Title, t.Artist, t.Album, t.Genre, t.DurationSeconds, t.ArtworkUrl, t.DateAddedSortKey, t.DateAddedText, t.DurationText, t.IsLiked, t.RemoteId";

    std::string TrackOrderClause(std::wstring const& filter, std::wstring const& sort)
    {
        auto normalizedFilter = ToLowerInvariant(filter);
        auto normalizedSort = ToLowerInvariant(sort);
        if (normalizedFilter == L"history")
        {
            return "ORDER BY LastPlayedOrder DESC, Title COLLATE NOCASE ASC";
        }
        if (normalizedFilter == L"most" || normalizedSort == L"mostplayed")
        {
            return "ORDER BY PlayCount DESC, LastPlayedOrder DESC, Title COLLATE NOCASE ASC";
        }
        if (normalizedSort == L"title")
        {
            return "ORDER BY Title COLLATE NOCASE ASC";
        }
        if (normalizedSort == L"artist")
        {
            return "ORDER BY Artist COLLATE NOCASE ASC, Title COLLATE NOCASE ASC";
        }
        if (normalizedSort == L"duration")
        {
            return "ORDER BY DurationSeconds DESC, Title COLLATE NOCASE ASC";
        }
        return "ORDER BY DateAddedSortKey DESC, Title COLLATE NOCASE ASC";
    }

    std::string TrackFilterClause(std::wstring const& filter)
    {
        auto normalizedFilter = ToLowerInvariant(filter);
        if (normalizedFilter == L"history")
        {
            return " AND COALESCE(LastPlayedOrder,0)>0";
        }
        if (normalizedFilter == L"most")
        {
            return " AND COALESCE(PlayCount,0)>0";
        }
        if (normalizedFilter == L"fav" || normalizedFilter == L"liked")
        {
            return " AND COALESCE(IsLiked,0)=1";
        }
        return {};
    }

    std::string GroupFilterClause(std::wstring const& groupKind)
    {
        auto normalizedKind = ToLowerInvariant(groupKind);
        if (normalizedKind == L"album")
        {
            return " AND CASE WHEN Album IS NULL OR Album='' THEN CASE WHEN SourceKind='remote' THEN 'Remote Singles' ELSE 'Unknown Album' END ELSE Album END = ?3";
        }
        if (normalizedKind == L"artist")
        {
            return " AND CASE WHEN Artist IS NULL OR Artist='' THEN 'Unknown Artist' ELSE Artist END = ?3";
        }
        if (normalizedKind == L"genre")
        {
            return " AND CASE WHEN Genre IS NULL OR Genre='' THEN 'Unknown Genre' ELSE Genre END = ?3";
        }
        return {};
    }
}