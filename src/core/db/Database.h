#pragma once

#include "core/app/AppPaths.h"
#include "core/async/Result.h"
#include "core/db/SqliteConnection.h"

namespace lmp::core::db {

// The library database, lastmusic.db in the data folder. Opening creates the
// folder and applies the connection settings earlier versions used, in the
// same order. Not thread-safe: it belongs to one owner thread, and other
// threads never touch the connection.
class Database {
public:
    explicit Database(AppPaths paths);

    // Creates the data folder, opens the file and applies the pragmas:
    // journal_mode=WAL, foreign_keys=ON, busy_timeout=5000,
    // synchronous=NORMAL, temp_store=MEMORY, cache_size=-16384,
    // mmap_size=268435456. As before, a pragma that fails does not stop the
    // open.
    [[nodiscard]] Result<void> open();
    void close();
    [[nodiscard]] bool isOpen() const { return m_connection.isOpen(); }

    // Each start goes back to the local-only context, as earlier versions
    // did; the mode service sets the real one afterwards. Needs the schema.
    [[nodiscard]] Result<void> resetActiveAccountContext();

    [[nodiscard]] SqliteConnection& connection() { return m_connection; }
    [[nodiscard]] const AppPaths& paths() const { return m_paths; }

private:
    AppPaths m_paths;
    SqliteConnection m_connection;
};

} // namespace lmp::core::db
