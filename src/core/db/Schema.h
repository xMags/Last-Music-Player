#pragma once

#include "core/async/Result.h"

#include <QString>
#include <QtGlobal>

namespace lmp::core::db {

class SqliteConnection;

// Creates the tables, indexes and the EffectiveTracks view, and migrates a
// database written by an earlier version up to the current schema version.
// The SQL is the earlier versions' text, unchanged; only the C++ around it is
// new. Steps that may fail on some older shapes are tried and their failure
// ignored, exactly as before; the others stop the step with a failure.
class Schema {
public:
    static constexpr int CurrentVersion = 14;

    // Safe to run on every start: the migration runs only when the stored
    // user_version is lower, and the rest is idempotent.
    [[nodiscard]] static Result<void> ensure(SqliteConnection& connection);

    // The stored PRAGMA user_version, or 0 when it cannot be read.
    [[nodiscard]] static int userVersion(SqliteConnection& connection);

private:
    [[nodiscard]] static Result<void> createTables(SqliteConnection& connection);
    static void migrateColumns(SqliteConnection& connection, int fromVersion);
    [[nodiscard]] static Result<void> migrateToSourceKeys(SqliteConnection& connection);
    [[nodiscard]] static Result<void> createIndexesAndView(SqliteConnection& connection);
    [[nodiscard]] static bool tracksHasLegacyFilePathUnique(SqliteConnection& connection);
    [[nodiscard]] static Result<void> runInTransaction(SqliteConnection& connection, const char* sql);
    [[nodiscard]] static Result<void> migrateImportedAlbumCollectionsToPlaylists(SqliteConnection& connection);
};

} // namespace lmp::core::db
