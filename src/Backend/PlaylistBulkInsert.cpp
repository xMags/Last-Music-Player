#include "pch.h"
#include "Backend/PlaylistBulkInsert.h"

#include "ThirdParty/sqlite/sqlite3.h"

namespace LastMusicPlayer::Backend
{
    namespace
    {
        class Statement final
        {
        public:
            Statement(sqlite3* database, char const* sql)
            {
                if (database)
                {
                    sqlite3_prepare_v2(database, sql, -1, &m_value, nullptr);
                }
            }

            ~Statement()
            {
                if (m_value)
                {
                    sqlite3_finalize(m_value);
                }
            }

            Statement(Statement const&) = delete;
            Statement& operator=(Statement const&) = delete;

            explicit operator bool() const noexcept { return m_value != nullptr; }
            sqlite3_stmt* Get() const noexcept { return m_value; }

        private:
            sqlite3_stmt* m_value{};
        };

        bool Execute(sqlite3* database, char const* sql)
        {
            return database && sqlite3_exec(database, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
        }

        bool BindText(sqlite3_stmt* statement, int index, std::wstring const& value)
        {
            return sqlite3_bind_text16(
                statement,
                index,
                value.c_str(),
                static_cast<int>(value.size() * sizeof(wchar_t)),
                SQLITE_TRANSIENT) == SQLITE_OK;
        }
    }

    bool AddTracksToPlaylistTransactional(
        sqlite3* database,
        std::wstring const& playlistKey,
        std::vector<std::int64_t> const& trackIds)
    {
        if (!database || playlistKey.empty() || trackIds.empty())
        {
            return false;
        }
        for (auto const trackId : trackIds)
        {
            if (trackId <= 0)
            {
                return false;
            }
        }

        if (!Execute(database, "BEGIN IMMEDIATE;"))
        {
            return false;
        }
        auto rollback = [&]()
        {
            (void)Execute(database, "ROLLBACK;");
            return false;
        };

        Statement playlist{ database, "SELECT Id FROM Playlists WHERE PlaylistKey=?1;" };
        if (!playlist || !BindText(playlist.Get(), 1, playlistKey)
            || sqlite3_step(playlist.Get()) != SQLITE_ROW)
        {
            return rollback();
        }
        auto const playlistId = sqlite3_column_int64(playlist.Get(), 0);

        Statement order{ database,
            "SELECT COALESCE(MAX(TrackOrder),0)+1 FROM PlaylistTracks WHERE PlaylistId=?1;" };
        if (!order
            || sqlite3_bind_int64(order.Get(), 1, playlistId) != SQLITE_OK
            || sqlite3_step(order.Get()) != SQLITE_ROW)
        {
            return rollback();
        }
        auto nextOrder = sqlite3_column_int(order.Get(), 0);

        Statement insert{ database,
            "INSERT OR IGNORE INTO PlaylistTracks (PlaylistId,TrackId,TrackOrder) VALUES (?1,?2,?3);" };
        if (!insert)
        {
            return rollback();
        }
        for (auto const trackId : trackIds)
        {
            sqlite3_reset(insert.Get());
            sqlite3_clear_bindings(insert.Get());
            if (sqlite3_bind_int64(insert.Get(), 1, playlistId) != SQLITE_OK
                || sqlite3_bind_int64(insert.Get(), 2, trackId) != SQLITE_OK
                || sqlite3_bind_int(insert.Get(), 3, nextOrder) != SQLITE_OK
                || sqlite3_step(insert.Get()) != SQLITE_DONE)
            {
                return rollback();
            }
            if (sqlite3_changes(database) > 0)
            {
                ++nextOrder;
            }
        }

        Statement touch{ database,
            "UPDATE Playlists SET UpdatedAt=strftime('%s','now') WHERE Id=?1;" };
        if (!touch
            || sqlite3_bind_int64(touch.Get(), 1, playlistId) != SQLITE_OK
            || sqlite3_step(touch.Get()) != SQLITE_DONE)
        {
            return rollback();
        }

        if (!Execute(database, "COMMIT;"))
        {
            return rollback();
        }
        return true;
    }
}
