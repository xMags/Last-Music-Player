#include "core/db/SqliteConnection.h"

#include "core/text/StrictUtf8.h"

#include <sqlite3.h>

#include <QByteArrayView>

#include <limits>
#include <utility>

namespace lmp::core::db {

namespace {

Failure storageFailure(sqlite3* connection, const char* what)
{
    const QString message = connection != nullptr ? QString::fromUtf8(sqlite3_errmsg(connection)) : QString();
    return Failure::of(FailureKind::Storage, QString::fromLatin1(what) + QStringLiteral(": ") + message);
}

} // namespace

SqliteStatement::SqliteStatement(sqlite3_stmt* statement, sqlite3* connection)
    : m_statement(statement)
    , m_connection(connection)
{
}

SqliteStatement::~SqliteStatement()
{
    finalize();
}

SqliteStatement::SqliteStatement(SqliteStatement&& other) noexcept
    : m_statement(std::exchange(other.m_statement, nullptr))
    , m_connection(std::exchange(other.m_connection, nullptr))
{
}

SqliteStatement& SqliteStatement::operator=(SqliteStatement&& other) noexcept
{
    if (this != &other) {
        finalize();
        m_statement = std::exchange(other.m_statement, nullptr);
        m_connection = std::exchange(other.m_connection, nullptr);
    }
    return *this;
}

void SqliteStatement::finalize()
{
    if (m_statement != nullptr) {
        sqlite3_finalize(m_statement);
        m_statement = nullptr;
    }
}

void SqliteStatement::bindText(int index, const QString& value)
{
    const std::optional<QByteArray> utf8 = text::encodeUtf8Strict(value);
    if (!utf8 || utf8->size() > std::numeric_limits<int>::max()) {
        sqlite3_bind_null(m_statement, index);
        return;
    }
    sqlite3_bind_text(m_statement, index, utf8->constData(), static_cast<int>(utf8->size()), SQLITE_TRANSIENT);
}

void SqliteStatement::bindInt64(int index, qint64 value)
{
    sqlite3_bind_int64(m_statement, index, value);
}

void SqliteStatement::bindDouble(int index, double value)
{
    sqlite3_bind_double(m_statement, index, value);
}

void SqliteStatement::bindNull(int index)
{
    sqlite3_bind_null(m_statement, index);
}

Result<bool> SqliteStatement::step()
{
    const int code = sqlite3_step(m_statement);
    if (code == SQLITE_ROW) {
        return true;
    }
    if (code == SQLITE_DONE) {
        return false;
    }
    return storageFailure(m_connection, "step");
}

Result<void> SqliteStatement::run()
{
    while (true) {
        const Result<bool> stepped = step();
        if (!stepped) {
            return stepped.failure();
        }
        if (!stepped.value()) {
            return Result<void>::success();
        }
    }
}

void SqliteStatement::reset()
{
    sqlite3_reset(m_statement);
}

void SqliteStatement::clearBindings()
{
    sqlite3_clear_bindings(m_statement);
}

int SqliteStatement::columnCount() const
{
    return sqlite3_column_count(m_statement);
}

bool SqliteStatement::columnIsNull(int column) const
{
    return sqlite3_column_type(m_statement, column) == SQLITE_NULL;
}

QString SqliteStatement::columnText(int column) const
{
    // sqlite3_column_text returns unsigned char; the bytes are UTF-8 text.
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(m_statement, column));
    if (text == nullptr) {
        return {};
    }
    const int size = sqlite3_column_bytes(m_statement, column);
    return text::decodeUtf8Strict(QByteArrayView(text, size)).value_or(QString());
}

qint64 SqliteStatement::columnInt64(int column) const
{
    return sqlite3_column_int64(m_statement, column);
}

double SqliteStatement::columnDouble(int column) const
{
    return sqlite3_column_double(m_statement, column);
}

SqliteConnection::~SqliteConnection()
{
    close();
}

Result<void> SqliteConnection::open(const QString& path)
{
    close();
    const std::optional<QByteArray> utf8 = text::encodeUtf8Strict(path);
    if (!utf8) {
        return Failure::of(FailureKind::Storage, QStringLiteral("database path is not valid text"));
    }
    if (sqlite3_open(utf8->constData(), &m_connection) != SQLITE_OK) {
        const Failure failure = storageFailure(m_connection, "open");
        close();
        return failure;
    }
    return Result<void>::success();
}

void SqliteConnection::close()
{
    if (m_connection != nullptr) {
        sqlite3_close(m_connection);
        m_connection = nullptr;
    }
}

Result<void> SqliteConnection::exec(const char* sql)
{
    char* error = nullptr;
    if (sqlite3_exec(m_connection, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        const QString message = error != nullptr ? QString::fromUtf8(error) : lastError();
        sqlite3_free(error);
        return Failure::of(FailureKind::Storage, message);
    }
    return Result<void>::success();
}

Result<SqliteStatement> SqliteConnection::prepare(const char* sql)
{
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(m_connection, sql, -1, &statement, nullptr) != SQLITE_OK || statement == nullptr) {
        sqlite3_finalize(statement);
        return storageFailure(m_connection, "prepare");
    }
    return SqliteStatement(statement, m_connection);
}

Result<SqliteStatement> SqliteConnection::prepare(const QByteArray& sql)
{
    return prepare(sql.constData());
}

qint64 SqliteConnection::lastInsertRowId() const
{
    return sqlite3_last_insert_rowid(m_connection);
}

int SqliteConnection::changes() const
{
    return sqlite3_changes(m_connection);
}

QString SqliteConnection::lastError() const
{
    return m_connection != nullptr ? QString::fromUtf8(sqlite3_errmsg(m_connection)) : QString();
}

} // namespace lmp::core::db
