#pragma once

#include "core/async/Result.h"
#include "core/db/SqliteConnection.h"

#include <QString>

// Small pieces the repositories share for explicit transactions and for
// turning a refused operation into a Failure.
namespace lmp::core::db::sql_transaction {

// Runs SQL whose failure is deliberately ignored: a rollback after an error,
// or clean-up that must not fail the operation around it.
inline void tryExec(SqliteConnection& connection, const char* sql)
{
    static_cast<void>(connection.exec(sql));
}

inline void rollback(SqliteConnection& connection)
{
    tryExec(connection, "ROLLBACK;");
}

// Rolls back and hands the failure on, for `return rollbackWith(...)`.
[[nodiscard]] inline Failure rollbackWith(SqliteConnection& connection, Failure failure)
{
    rollback(connection);
    return failure;
}

// The SQLite error of the last call, as a storage failure.
[[nodiscard]] inline Failure storageFailure(const SqliteConnection& connection, const QString& what)
{
    return Failure::of(FailureKind::Storage, what + QStringLiteral(": ") + connection.lastError());
}

// The operation found nothing to act on (an empty key, a missing row, no
// change), which earlier versions reported as a plain false or zero.
[[nodiscard]] inline Failure notFound(const QString& what)
{
    return Failure::of(FailureKind::NotFound, what);
}

} // namespace lmp::core::db::sql_transaction
