#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QTest>

#include "core/app/AppPaths.h"
#include "core/db/Database.h"

using lmp::core::AppPaths;
using lmp::core::db::Database;
using lmp::core::db::SqliteConnection;
using lmp::core::db::SqliteStatement;

namespace {

QString pragmaText(SqliteConnection& connection, const char* sql)
{
    auto statement = connection.prepare(sql);
    if (!statement || !statement.value().step().valueOr(false)) {
        return QStringLiteral("<none>");
    }
    return statement.value().columnText(0);
}

} // namespace

class DatabaseOpenTest : public QObject {
    Q_OBJECT

private slots:
    void opensInANewFolder();
    void pragmasReadBack();
    void builtWithTheContractFlags();
    void activeContextResetsToLocalOnly();
    void textRoundTripsAndBadTextBindsNull();
};

void DatabaseOpenTest::opensInANewFolder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const AppPaths paths(dir.filePath(QStringLiteral("nested/Last Music Player")));
    Database database(paths);
    const auto opened = database.open();
    QVERIFY2(opened.isOk(), qPrintable(opened.isOk() ? QString() : opened.failure().detail));
    QVERIFY(database.isOpen());
    QVERIFY(QFileInfo::exists(paths.databaseFile()));
    database.close();
    QVERIFY(!database.isOpen());
}

void DatabaseOpenTest::pragmasReadBack()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database database{AppPaths(dir.path())};
    QVERIFY(database.open().isOk());
    SqliteConnection& connection = database.connection();
    QCOMPARE(pragmaText(connection, "PRAGMA journal_mode;"), QStringLiteral("wal"));
    QCOMPARE(pragmaText(connection, "PRAGMA foreign_keys;"), QStringLiteral("1"));
    QCOMPARE(pragmaText(connection, "PRAGMA busy_timeout;"), QStringLiteral("5000"));
    QCOMPARE(pragmaText(connection, "PRAGMA synchronous;"), QStringLiteral("1"));
    QCOMPARE(pragmaText(connection, "PRAGMA temp_store;"), QStringLiteral("2"));
    QCOMPARE(pragmaText(connection, "PRAGMA cache_size;"), QStringLiteral("-16384"));
    QCOMPARE(pragmaText(connection, "PRAGMA mmap_size;"), QStringLiteral("268435456"));
}

void DatabaseOpenTest::builtWithTheContractFlags()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database database{AppPaths(dir.path())};
    QVERIFY(database.open().isOk());
    auto statement = database.connection().prepare("PRAGMA compile_options;");
    QVERIFY(statement.isOk());
    QStringList options;
    while (statement.value().step().valueOr(false)) {
        options << statement.value().columnText(0);
    }
    QVERIFY2(options.contains(QStringLiteral("THREADSAFE=2")), qPrintable(options.join(u' ')));
    QVERIFY(options.contains(QStringLiteral("DEFAULT_WAL_SYNCHRONOUS=1")));
    QVERIFY(options.contains(QStringLiteral("LIKE_DOESNT_MATCH_BLOBS")));
    QVERIFY(options.contains(QStringLiteral("OMIT_DEPRECATED")));
    QVERIFY(options.contains(QStringLiteral("DEFAULT_MEMSTATUS=0")));
}

void DatabaseOpenTest::activeContextResetsToLocalOnly()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database database{AppPaths(dir.path())};
    QVERIFY(database.open().isOk());
    // Without the table the reset fails cleanly.
    QVERIFY(!database.resetActiveAccountContext().isOk());
    QVERIFY(database.connection()
                .exec("CREATE TABLE ActiveAccountContext (SingletonId INTEGER PRIMARY KEY CHECK(SingletonId=1), "
                      "RemoteMode TEXT NOT NULL DEFAULT 'LocalOnly', AccountId TEXT);"
                      "INSERT INTO ActiveAccountContext VALUES (1, 'Account', 'someone');")
                .isOk());
    QVERIFY(database.resetActiveAccountContext().isOk());
    QCOMPARE(pragmaText(database.connection(), "SELECT RemoteMode || '|' || AccountId FROM ActiveAccountContext;"),
             QStringLiteral("LocalOnly|"));
}

void DatabaseOpenTest::textRoundTripsAndBadTextBindsNull()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Database database{AppPaths(dir.path())};
    QVERIFY(database.open().isOk());
    SqliteConnection& connection = database.connection();
    QVERIFY(connection.exec("CREATE TABLE T (V TEXT);").isOk());
    auto insert = connection.prepare("INSERT INTO T (V) VALUES (?);");
    QVERIFY(insert.isOk());
    const QString text = QStringLiteral("Café ") + QString::fromUcs4(U"\U0001F3B5");
    insert.value().bindText(1, text);
    QVERIFY(insert.value().run().isOk());
    insert.value().reset();
    insert.value().bindText(1, QString(QChar(0xD800)));
    QVERIFY(insert.value().run().isOk());

    auto select = connection.prepare("SELECT V FROM T ORDER BY rowid;");
    QVERIFY(select.isOk());
    QVERIFY(select.value().step().valueOr(false));
    QCOMPARE(select.value().columnText(0), text);
    QVERIFY(select.value().step().valueOr(false));
    QVERIFY(select.value().columnIsNull(0));
    QVERIFY(!select.value().step().valueOr(true));
}

QTEST_GUILESS_MAIN(DatabaseOpenTest)
#include "tst_database_open.moc"
