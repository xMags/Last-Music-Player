#include "pch.h"
#include "Backend/DatabaseEngine.h"
#include "Backend/DatabaseEngine.Internal.h"
#include "Backend/ProviderHelpers.h"

namespace LastMusicPlayer::Backend
{
    using namespace DatabaseDetail;

    int64_t DatabaseEngine::CreateAlbumCollection(std::wstring const& title)
    {
        std::scoped_lock lock{ m_mutex };
        auto cleanedTitle = title.empty() ? std::wstring{ L"Untitled Album" } : title;
        TrackInfo album;
        album.Title(winrt::hstring(cleanedTitle));
        album.Artist(L"Your hand-picked songs live here.");
        album.SourceKind(L"manual");
        album.SourceLabel(L"Manual");

        auto albumKey = L"manual|" + TimestampKeyPart() + L"|" + std::to_wstring(std::hash<std::wstring>{}(cleanedTitle));
        return UpsertAlbumCollection(album, albumKey);
    }

    int64_t DatabaseEngine::UpsertAlbumCollection(TrackInfo const& album, std::wstring const& albumKey)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || albumKey.empty())
        {
            return 0;
        }

        static constexpr char kSql[] =
            "INSERT INTO Albums (AlbumKey, Title, Artist, ArtworkUrl, SourceKind, SourceUrl, SourceLabel, CreatedAt, UpdatedAt) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, strftime('%s','now'), strftime('%s','now')) "
            "ON CONFLICT(AlbumKey) DO UPDATE SET "
            "Title=excluded.Title, Artist=excluded.Artist, ArtworkUrl=excluded.ArtworkUrl, "
            "SourceKind=excluded.SourceKind, SourceUrl=excluded.SourceUrl, SourceLabel=excluded.SourceLabel, UpdatedAt=excluded.UpdatedAt;";

        Statement stmt{ m_db, kSql };
        if (!stmt)
        {
            return 0;
        }

        auto sourceKind = std::wstring(album.SourceKind().c_str());
        auto sourceLabel = std::wstring(album.SourceLabel().c_str());
        BindText(stmt.value, 1, albumKey);
        BindText(stmt.value, 2, std::wstring(album.Title().c_str()));
        BindText(stmt.value, 3, std::wstring(album.Artist().c_str()));
        auto artworkUrl = std::wstring(album.ArtworkUrl().c_str());
        if (sourceKind != L"manual"
            && !IsSafeRemoteUrl(winrt::hstring(artworkUrl), RemoteUrlUse::Durable))
        {
            artworkUrl.clear();
        }
        BindText(stmt.value, 4, artworkUrl);
        BindText(stmt.value, 5, sourceKind.empty() ? std::wstring{ L"manual" } : sourceKind);
        BindText(stmt.value, 6, std::wstring(album.SourceUrl().c_str()));
        BindText(stmt.value, 7, sourceLabel.empty() ? std::wstring{ L"Album" } : sourceLabel);

        if (sqlite3_step(stmt.value) != SQLITE_DONE)
        {
            ::OutputDebugStringA(sqlite3_errmsg(m_db));
            return 0;
        }

        Statement lookup{ m_db, "SELECT Id FROM Albums WHERE AlbumKey=?;" };
        if (!lookup)
        {
            return 0;
        }
        BindText(lookup.value, 1, albumKey);
        if (sqlite3_step(lookup.value) == SQLITE_ROW)
        {
            return sqlite3_column_int64(lookup.value, 0);
        }
        return sqlite3_last_insert_rowid(m_db);
    }

    void DatabaseEngine::ReplaceAlbumCollectionTracks(int64_t albumId, std::vector<int64_t> const& trackIds)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || albumId <= 0)
        {
            return;
        }

        if (!Exec(m_db, "BEGIN IMMEDIATE;"))
        {
            return;
        }

        Statement del{ m_db, "DELETE FROM AlbumTracks WHERE AlbumId=?;" };
        if (!del)
        {
            TryExec(m_db, "ROLLBACK;");
            return;
        }
        sqlite3_bind_int64(del.value, 1, albumId);
        if (sqlite3_step(del.value) != SQLITE_DONE)
        {
            TryExec(m_db, "ROLLBACK;");
            return;
        }

        Statement insert{ m_db, "INSERT OR IGNORE INTO AlbumTracks (AlbumId, TrackId, TrackOrder) VALUES (?, ?, ?);" };
        if (!insert)
        {
            TryExec(m_db, "ROLLBACK;");
            return;
        }

        int order = 1;
        for (auto trackId : trackIds)
        {
            if (trackId <= 0)
            {
                continue;
            }
            sqlite3_reset(insert.value);
            sqlite3_clear_bindings(insert.value);
            sqlite3_bind_int64(insert.value, 1, albumId);
            sqlite3_bind_int64(insert.value, 2, trackId);
            sqlite3_bind_int(insert.value, 3, order++);
            if (sqlite3_step(insert.value) != SQLITE_DONE)
            {
                TryExec(m_db, "ROLLBACK;");
                return;
            }
        }

        Statement touch{ m_db, "UPDATE Albums SET UpdatedAt=strftime('%s','now') WHERE Id=?;" };
        if (touch)
        {
            sqlite3_bind_int64(touch.value, 1, albumId);
            sqlite3_step(touch.value);
        }
        if (!Exec(m_db, "COMMIT;"))
        {
            TryExec(m_db, "ROLLBACK;");
        }
    }

    bool DatabaseEngine::DeleteAlbumCollection(std::wstring const& albumKey)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || albumKey.empty())
        {
            return false;
        }

        Statement lookup{ m_db, "SELECT Id, SourceKind FROM Albums WHERE AlbumKey=?;" };
        if (!lookup)
        {
            return false;
        }
        BindText(lookup.value, 1, albumKey);
        if (sqlite3_step(lookup.value) != SQLITE_ROW)
        {
            return false;
        }

        auto albumId = sqlite3_column_int64(lookup.value, 0);
        auto sourceKind = ToLowerInvariant(ColumnText(lookup.value, 1));
        if (albumId <= 0)
        {
            return false;
        }

        if (!Exec(m_db, "BEGIN IMMEDIATE;"))
        {
            return false;
        }

        if (sourceKind != L"manual")
        {
            Statement deleteImportedTracks{ m_db,
                "DELETE FROM Tracks WHERE SourceKind='remote' AND Id IN ("
                "SELECT at.TrackId FROM AlbumTracks at "
                "WHERE at.AlbumId=?1 AND NOT EXISTS ("
                "SELECT 1 FROM AlbumTracks other WHERE other.TrackId=at.TrackId AND other.AlbumId<>?1) "
                "AND NOT EXISTS (SELECT 1 FROM PlaylistTracks pt WHERE pt.TrackId=at.TrackId));" };
            if (!deleteImportedTracks)
            {
                TryExec(m_db, "ROLLBACK;");
                return false;
            }
            sqlite3_bind_int64(deleteImportedTracks.value, 1, albumId);
            if (sqlite3_step(deleteImportedTracks.value) != SQLITE_DONE)
            {
                TryExec(m_db, "ROLLBACK;");
                return false;
            }
        }

        Statement deleteLinks{ m_db, "DELETE FROM AlbumTracks WHERE AlbumId=?;" };
        if (!deleteLinks)
        {
            TryExec(m_db, "ROLLBACK;");
            return false;
        }
        sqlite3_bind_int64(deleteLinks.value, 1, albumId);
        if (sqlite3_step(deleteLinks.value) != SQLITE_DONE)
        {
            TryExec(m_db, "ROLLBACK;");
            return false;
        }

        Statement deleteAlbum{ m_db, "DELETE FROM Albums WHERE Id=?;" };
        if (!deleteAlbum)
        {
            TryExec(m_db, "ROLLBACK;");
            return false;
        }
        sqlite3_bind_int64(deleteAlbum.value, 1, albumId);
        if (sqlite3_step(deleteAlbum.value) != SQLITE_DONE)
        {
            TryExec(m_db, "ROLLBACK;");
            return false;
        }

        if (!Exec(m_db, "COMMIT;"))
        {
            TryExec(m_db, "ROLLBACK;");
            return false;
        }
        return true;
    }

    int64_t DatabaseEngine::CreatePlaylist(std::wstring const& title)
    {
        std::scoped_lock lock{ m_mutex };
        auto cleanedTitle = title.empty() ? std::wstring{ L"Untitled Playlist" } : title;
        TrackInfo playlist;
        playlist.Title(winrt::hstring(cleanedTitle));
        playlist.Artist(L"Add songs from your library.");
        playlist.SourceKind(L"manual");
        playlist.Provider(L"manual");
        playlist.SourceLabel(L"Manual");

        auto playlistKey = L"manual|" + TimestampKeyPart() + L"|" + std::to_wstring(std::hash<std::wstring>{}(cleanedTitle));
        return UpsertPlaylist(playlist, playlistKey);
    }

    int64_t DatabaseEngine::UpsertPlaylist(TrackInfo const& playlist, std::wstring const& playlistKey)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || playlistKey.empty())
        {
            return 0;
        }

        static constexpr char kSql[] =
            "INSERT INTO Playlists (PlaylistKey, Title, Description, ArtworkUrl, SourceKind, Provider, SourceUrl, SourceLabel, CreatedAt, UpdatedAt) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, strftime('%s','now'), strftime('%s','now')) "
            "ON CONFLICT(PlaylistKey) DO UPDATE SET "
            "Title=excluded.Title, Description=excluded.Description, ArtworkUrl=excluded.ArtworkUrl, "
            "SourceKind=excluded.SourceKind, Provider=excluded.Provider, SourceUrl=excluded.SourceUrl, SourceLabel=excluded.SourceLabel, UpdatedAt=excluded.UpdatedAt;";

        Statement stmt{ m_db, kSql };
        if (!stmt)
        {
            return 0;
        }

        auto sourceKind = std::wstring(playlist.SourceKind().c_str());
        auto provider = std::wstring(playlist.Provider().c_str());
        auto sourceLabel = std::wstring(playlist.SourceLabel().c_str());
        BindText(stmt.value, 1, playlistKey);
        BindText(stmt.value, 2, std::wstring(playlist.Title().c_str()));
        BindText(stmt.value, 3, std::wstring(playlist.Artist().c_str()));
        auto artworkUrl = std::wstring(playlist.ArtworkUrl().c_str());
        if (provider != L"manual"
            && !IsSafeRemoteUrl(winrt::hstring(artworkUrl), RemoteUrlUse::Durable))
        {
            artworkUrl.clear();
        }
        BindText(stmt.value, 4, artworkUrl);
        BindText(stmt.value, 5, sourceKind.empty() ? std::wstring{ L"manual" } : sourceKind);
        BindText(stmt.value, 6, provider.empty() ? std::wstring{ L"manual" } : provider);
        BindText(stmt.value, 7, std::wstring(playlist.SourceUrl().c_str()));
        BindText(stmt.value, 8, sourceLabel.empty() ? std::wstring{ L"Manual" } : sourceLabel);

        if (sqlite3_step(stmt.value) != SQLITE_DONE)
        {
            ::OutputDebugStringA(sqlite3_errmsg(m_db));
            return 0;
        }

        Statement lookup{ m_db, "SELECT Id FROM Playlists WHERE PlaylistKey=?;" };
        if (!lookup)
        {
            return 0;
        }
        BindText(lookup.value, 1, playlistKey);
        if (sqlite3_step(lookup.value) == SQLITE_ROW)
        {
            return sqlite3_column_int64(lookup.value, 0);
        }
        return sqlite3_last_insert_rowid(m_db);
    }

    void DatabaseEngine::ReplacePlaylistTracks(int64_t playlistId, std::vector<int64_t> const& trackIds)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || playlistId <= 0)
        {
            return;
        }

        if (!Exec(m_db, "BEGIN IMMEDIATE;"))
        {
            return;
        }

        Statement del{ m_db, "DELETE FROM PlaylistTracks WHERE PlaylistId=?;" };
        if (!del)
        {
            TryExec(m_db, "ROLLBACK;");
            return;
        }
        sqlite3_bind_int64(del.value, 1, playlistId);
        if (sqlite3_step(del.value) != SQLITE_DONE)
        {
            TryExec(m_db, "ROLLBACK;");
            return;
        }

        Statement insert{ m_db, "INSERT OR IGNORE INTO PlaylistTracks (PlaylistId, TrackId, TrackOrder) VALUES (?, ?, ?);" };
        if (!insert)
        {
            TryExec(m_db, "ROLLBACK;");
            return;
        }

        int order = 1;
        for (auto trackId : trackIds)
        {
            if (trackId <= 0)
            {
                continue;
            }
            sqlite3_reset(insert.value);
            sqlite3_clear_bindings(insert.value);
            sqlite3_bind_int64(insert.value, 1, playlistId);
            sqlite3_bind_int64(insert.value, 2, trackId);
            sqlite3_bind_int(insert.value, 3, order++);
            if (sqlite3_step(insert.value) != SQLITE_DONE)
            {
                TryExec(m_db, "ROLLBACK;");
                return;
            }
        }

        Statement touch{ m_db, "UPDATE Playlists SET UpdatedAt=strftime('%s','now') WHERE Id=?;" };
        if (touch)
        {
            sqlite3_bind_int64(touch.value, 1, playlistId);
            sqlite3_step(touch.value);
        }
        if (!Exec(m_db, "COMMIT;"))
        {
            TryExec(m_db, "ROLLBACK;");
        }
    }
    bool DatabaseEngine::ReorderPlaylistTracks(
        std::wstring const& playlistKey,
        std::vector<int64_t> const& trackIds)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || playlistKey.empty())
        {
            return false;
        }

        Statement lookup{ m_db, "SELECT Id FROM Playlists WHERE PlaylistKey=?;" };
        if (!lookup)
        {
            return false;
        }
        BindText(lookup.value, 1, playlistKey);
        if (sqlite3_step(lookup.value) != SQLITE_ROW)
        {
            return false;
        }
        auto playlistId = sqlite3_column_int64(lookup.value, 0);
        ReplacePlaylistTracks(playlistId, trackIds);

        Statement verify{ m_db, "SELECT TrackId FROM PlaylistTracks WHERE PlaylistId=? ORDER BY TrackOrder;" };
        if (!verify)
        {
            return false;
        }
        sqlite3_bind_int64(verify.value, 1, playlistId);
        std::size_t index{};
        while (sqlite3_step(verify.value) == SQLITE_ROW)
        {
            if (index >= trackIds.size() || sqlite3_column_int64(verify.value, 0) != trackIds[index])
            {
                return false;
            }
            ++index;
        }
        return index == trackIds.size();
    }


    bool DatabaseEngine::AddTrackToPlaylist(std::wstring const& playlistKey, int64_t trackId)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || playlistKey.empty() || trackId <= 0)
        {
            return false;
        }

        Statement lookup{ m_db, "SELECT Id FROM Playlists WHERE PlaylistKey=?;" };
        if (!lookup)
        {
            return false;
        }
        BindText(lookup.value, 1, playlistKey);
        if (sqlite3_step(lookup.value) != SQLITE_ROW)
        {
            return false;
        }

        auto playlistId = sqlite3_column_int64(lookup.value, 0);
        Statement order{ m_db, "SELECT COALESCE(MAX(TrackOrder),0)+1 FROM PlaylistTracks WHERE PlaylistId=?;" };
        int nextOrder = 1;
        if (order)
        {
            sqlite3_bind_int64(order.value, 1, playlistId);
            if (sqlite3_step(order.value) == SQLITE_ROW)
            {
                nextOrder = sqlite3_column_int(order.value, 0);
            }
        }

        Statement insert{ m_db, "INSERT OR IGNORE INTO PlaylistTracks (PlaylistId, TrackId, TrackOrder) VALUES (?, ?, ?);" };
        if (!insert)
        {
            return false;
        }
        sqlite3_bind_int64(insert.value, 1, playlistId);
        sqlite3_bind_int64(insert.value, 2, trackId);
        sqlite3_bind_int(insert.value, 3, nextOrder);
        if (sqlite3_step(insert.value) != SQLITE_DONE)
        {
            return false;
        }

        Statement touch{ m_db, "UPDATE Playlists SET UpdatedAt=strftime('%s','now') WHERE Id=?;" };
        if (touch)
        {
            sqlite3_bind_int64(touch.value, 1, playlistId);
            sqlite3_step(touch.value);
        }
        return true;
    }

    bool DatabaseEngine::RemoveTrackFromPlaylist(std::wstring const& playlistKey, int64_t trackId)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || playlistKey.empty() || trackId <= 0)
        {
            return false;
        }

        Statement del{ m_db,
            "DELETE FROM PlaylistTracks WHERE TrackId=?1 AND PlaylistId=(SELECT Id FROM Playlists WHERE PlaylistKey=?2);" };
        if (!del)
        {
            return false;
        }
        sqlite3_bind_int64(del.value, 1, trackId);
        BindText(del.value, 2, playlistKey);
        if (sqlite3_step(del.value) != SQLITE_DONE)
        {
            return false;
        }

        Statement touch{ m_db, "UPDATE Playlists SET UpdatedAt=strftime('%s','now') WHERE PlaylistKey=?;" };
        if (touch)
        {
            BindText(touch.value, 1, playlistKey);
            sqlite3_step(touch.value);
        }
        return true;
    }

    bool DatabaseEngine::RenamePlaylist(std::wstring const& playlistKey, std::wstring const& title)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || playlistKey.empty())
        {
            return false;
        }

        auto cleanedTitle = title.empty() ? std::wstring{ L"Untitled Playlist" } : title;
        Statement stmt{ m_db, "UPDATE Playlists SET Title=?, UpdatedAt=strftime('%s','now') WHERE PlaylistKey=?;" };
        if (!stmt)
        {
            return false;
        }
        BindText(stmt.value, 1, cleanedTitle);
        BindText(stmt.value, 2, playlistKey);
        return sqlite3_step(stmt.value) == SQLITE_DONE && sqlite3_changes(m_db) > 0;
    }

    bool DatabaseEngine::DeletePlaylist(std::wstring const& playlistKey)
    {
        std::scoped_lock lock{ m_mutex };
        if (!m_db || playlistKey.empty())
        {
            return false;
        }

        Statement stmt{ m_db, "DELETE FROM Playlists WHERE PlaylistKey=?;" };
        if (!stmt)
        {
            return false;
        }
        BindText(stmt.value, 1, playlistKey);
        return sqlite3_step(stmt.value) == SQLITE_DONE && sqlite3_changes(m_db) > 0;
    }
}
