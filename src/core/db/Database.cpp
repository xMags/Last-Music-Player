#include "core/db/Database.h"

#include <QDir>

#include <array>
#include <utility>

namespace lmp::core::db {

namespace {

// In the order earlier versions applied them. NORMAL is durable under WAL;
// in-memory temp tables, a 16 MB page cache and a 256 MB memory map cut I/O
// for bulk passes and every query.
constexpr std::array<const char*, 7> kPragmas{
    "PRAGMA journal_mode=WAL;",
    "PRAGMA foreign_keys=ON;",
    "PRAGMA busy_timeout=5000;",
    "PRAGMA synchronous=NORMAL;",
    "PRAGMA temp_store=MEMORY;",
    "PRAGMA cache_size=-16384;",
    "PRAGMA mmap_size=268435456;",
};

} // namespace

Database::Database(AppPaths paths)
    : m_paths(std::move(paths))
{
}

Result<void> Database::open()
{
    if (!QDir().mkpath(m_paths.root())) {
        return Failure::of(FailureKind::Storage, QStringLiteral("could not create the data folder"));
    }
    const Result<void> opened = m_connection.open(m_paths.databaseFile());
    if (!opened) {
        return opened;
    }
    for (const char* pragma : kPragmas) {
        // Earlier versions ignored a pragma that failed; so does this.
        static_cast<void>(m_connection.exec(pragma));
    }
    return Result<void>::success();
}

void Database::close()
{
    m_connection.close();
}

Result<void> Database::resetActiveAccountContext()
{
    return m_connection.exec(
        "INSERT INTO ActiveAccountContext (SingletonId, RemoteMode, AccountId) VALUES (1, 'LocalOnly', '') "
        "ON CONFLICT(SingletonId) DO UPDATE SET RemoteMode='LocalOnly', AccountId='';");
}

} // namespace lmp::core::db
