#pragma once

#include <QString>
#include <QTemporaryDir>
#include <QtGlobal>

#include "core/app/AppPaths.h"
#include "core/db/Database.h"

// A fresh, initialised library database in a temporary folder, for the
// repository and query tests, with one-value readers for checking rows.
class TempLibrary {
public:
    TempLibrary()
        : m_database(lmp::core::AppPaths(m_dir.path()))
    {
        m_ok = m_dir.isValid() && m_database.initialize().isOk();
    }

    [[nodiscard]] bool ok() const { return m_ok; }
    [[nodiscard]] lmp::core::db::Database& database() { return m_database; }
    [[nodiscard]] lmp::core::db::SqliteConnection& connection() { return m_database.connection(); }

    // The first column of the first row; "<none>" without a row, "<null>" for NULL.
    [[nodiscard]] QString text(const char* sql)
    {
        auto statement = connection().prepare(sql);
        if (!statement || !statement.value().step().valueOr(false)) {
            return QStringLiteral("<none>");
        }
        return statement.value().columnIsNull(0) ? QStringLiteral("<null>") : statement.value().columnText(0);
    }

    // The first column of the first row; -1 without a row.
    [[nodiscard]] qint64 number(const char* sql)
    {
        auto statement = connection().prepare(sql);
        if (!statement || !statement.value().step().valueOr(false)) {
            return -1;
        }
        return statement.value().columnInt64(0);
    }

private:
    QTemporaryDir m_dir;
    lmp::core::db::Database m_database;
    bool m_ok = false;
};
