#include "pch.h"
#include "Backend/DatabaseEngine.h"
#include "Backend/DatabaseEngine.Internal.h"

#include <filesystem>

namespace LastMusicPlayer::Backend
{
    using namespace DatabaseDetail;

    DatabaseEngine::~DatabaseEngine()
    {
        Close();
    }

    bool DatabaseEngine::Initialize()
    {
        std::scoped_lock lock{ m_mutex };
        auto appDataPath = AppDataDirectory();
        std::filesystem::create_directories(appDataPath);
        auto dbPath = WideToUtf8((appDataPath / L"lastmusic.db").wstring());

        if (sqlite3_open(dbPath.c_str(), &m_db) != SQLITE_OK)
        {
            if (m_db != nullptr)
            {
                ::OutputDebugStringA(sqlite3_errmsg(m_db));
            }
            Close();
            return false;
        }

        Exec(m_db, "PRAGMA journal_mode=WAL;");
        Exec(m_db, "PRAGMA foreign_keys=ON;");
        Exec(m_db, "PRAGMA busy_timeout=5000;");
        // Low-risk runtime tuning: NORMAL is durable under WAL, in-memory
        // temp tables make the migration's temp work fast, a larger page
        // cache and mmap cut I/O for the bulk passes and every query.
        Exec(m_db, "PRAGMA synchronous=NORMAL;");
        Exec(m_db, "PRAGMA temp_store=MEMORY;");
        Exec(m_db, "PRAGMA cache_size=-16384;");
        Exec(m_db, "PRAGMA mmap_size=268435456;");

        if (!EnsureSchema())
        {
            Close();
            return false;
        }

        if (!Exec(m_db, "INSERT INTO ActiveAccountContext (SingletonId, RemoteMode, AccountId) VALUES (1, 'LocalOnly', '') ON CONFLICT(SingletonId) DO UPDATE SET RemoteMode='LocalOnly', AccountId='';"))
        {
            Close();
            return false;
        }

        return true;
    }

    void DatabaseEngine::Close()
    {
        std::scoped_lock lock{ m_mutex };
        if (m_db != nullptr)
        {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
    }
}
