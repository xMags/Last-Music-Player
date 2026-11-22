#pragma once

#include "core/async/Result.h"

#include <QByteArray>
#include <QString>
#include <QtGlobal>

struct sqlite3;
struct sqlite3_stmt;

namespace lmp::core::db {

// One prepared statement. Move-only; finalised when destroyed. Text crosses
// the boundary as UTF-8: a parameter that is not valid text binds NULL and a
// column that is not valid UTF-8 reads as empty, as before.
class SqliteStatement {
public:
    SqliteStatement() = default;
    SqliteStatement(sqlite3_stmt* statement, sqlite3* connection);
    ~SqliteStatement();
    SqliteStatement(SqliteStatement&& other) noexcept;
    SqliteStatement& operator=(SqliteStatement&& other) noexcept;
    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;

    [[nodiscard]] bool isValid() const { return m_statement != nullptr; }

    // Parameters count from 1, as in SQL.
    void bindText(int index, const QString& value);
    void bindInt64(int index, qint64 value);
    void bindDouble(int index, double value);
    void bindNull(int index);

    // True while a row is available, false when the statement is done.
    [[nodiscard]] Result<bool> step();
    // Runs to completion, ignoring any rows.
    [[nodiscard]] Result<void> run();
    // Ready to run again; bindings are kept unless cleared.
    void reset();
    void clearBindings();

    // Columns count from 0.
    [[nodiscard]] int columnCount() const;
    [[nodiscard]] bool columnIsNull(int column) const;
    [[nodiscard]] QString columnText(int column) const;
    [[nodiscard]] qint64 columnInt64(int column) const;
    [[nodiscard]] double columnDouble(int column) const;

private:
    void finalize();

    sqlite3_stmt* m_statement = nullptr;
    sqlite3* m_connection = nullptr;
};

// One SQLite database connection, closed when destroyed. Not thread-safe: the
// library is built in its multi-thread mode, where a connection may be used by
// one thread at a time, so the connection belongs to one owner thread.
class SqliteConnection {
public:
    SqliteConnection() = default;
    ~SqliteConnection();
    SqliteConnection(const SqliteConnection&) = delete;
    SqliteConnection& operator=(const SqliteConnection&) = delete;

    // Opens (and creates) the database file at `path`.
    [[nodiscard]] Result<void> open(const QString& path);
    void close();
    [[nodiscard]] bool isOpen() const { return m_connection != nullptr; }

    // Runs one or more statements with no parameters.
    [[nodiscard]] Result<void> exec(const char* sql);
    [[nodiscard]] Result<SqliteStatement> prepare(const char* sql);
    [[nodiscard]] Result<SqliteStatement> prepare(const QByteArray& sql);

    [[nodiscard]] qint64 lastInsertRowId() const;
    // Rows changed by the most recent statement.
    [[nodiscard]] int changes() const;
    [[nodiscard]] QString lastError() const;

    [[nodiscard]] sqlite3* handle() const { return m_connection; }

private:
    sqlite3* m_connection = nullptr;
};

} // namespace lmp::core::db
