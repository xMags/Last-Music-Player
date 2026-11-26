#include <QFuture>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QThread>

#include <atomic>
#include <memory>
#include <thread>

#include "TempLibrary.h"
#include "core/db/DatabaseReader.h"

using lmp::core::db::Database;
using lmp::core::db::DatabaseReader;
using lmp::core::db::ThreadDatabaseReadRunner;

namespace {

[[nodiscard]] QString firstText(Database& database, const char* sql)
{
    auto statement = database.connection().prepare(sql);
    if (!statement || !statement.value().step().valueOr(false)) {
        return QStringLiteral("<none>");
    }
    return statement.value().columnText(0);
}

} // namespace

// Library reads on their own worker thread and connection.
class DatabaseReaderTest : public QObject {
    Q_OBJECT

private slots:
    void readsWhatTheMainConnectionCommitted();
    void itsConnectionCannotWrite();
    void nothingRunsAfterShutdown();
    void aReadUnderWayAtShutdownEndsCancelled();
};

void DatabaseReaderTest::readsWhatTheMainConnectionCommitted()
{
    TempLibrary library;
    QVERIFY(library.ok());
    QVERIFY(library.connection().exec("CREATE TABLE Probe (Value TEXT); INSERT INTO Probe VALUES ('one');").isOk());
    DatabaseReader reader(std::make_unique<ThreadDatabaseReadRunner>(library.database().paths()));

    QThread* readOn = nullptr;
    QFuture<QString> read = reader.read<QString>([&readOn](Database& database) {
        readOn = QThread::currentThread();
        return firstText(database, "SELECT Value FROM Probe;");
    });
    QTRY_VERIFY(read.isFinished());
    QCOMPARE(read.result(), QStringLiteral("one"));
    QVERIFY(readOn != QThread::currentThread());

    // A later commit is seen by a later read.
    QVERIFY(library.connection().exec("UPDATE Probe SET Value = 'two';").isOk());
    QFuture<QString> again = reader.read<QString>([](Database& database) {
        return firstText(database, "SELECT Value FROM Probe;");
    });
    QTRY_VERIFY(again.isFinished());
    QCOMPARE(again.result(), QStringLiteral("two"));
}

void DatabaseReaderTest::itsConnectionCannotWrite()
{
    TempLibrary library;
    QVERIFY(library.ok());
    QVERIFY(library.connection().exec("CREATE TABLE Probe (Value TEXT);").isOk());
    DatabaseReader reader(std::make_unique<ThreadDatabaseReadRunner>(library.database().paths()));
    QFuture<bool> wrote = reader.read<bool>([](Database& database) {
        return database.connection().exec("INSERT INTO Probe VALUES ('x');").isOk();
    });
    QTRY_VERIFY(wrote.isFinished());
    QVERIFY(!wrote.result());
    QCOMPARE(library.number("SELECT COUNT(*) FROM Probe;"), 0);
}

void DatabaseReaderTest::nothingRunsAfterShutdown()
{
    TempLibrary library;
    QVERIFY(library.ok());
    DatabaseReader reader(std::make_unique<ThreadDatabaseReadRunner>(library.database().paths()));
    reader.shutdown();
    reader.shutdown(); // twice is safe

    bool ran = false;
    QFuture<int> read = reader.read<int>([&ran](Database&) {
        ran = true;
        return 1;
    });
    QVERIFY(read.isFinished());
    QVERIFY(read.isCanceled());
    // A continuation on the context never runs.
    bool continued = false;
    QObject context;
    read.then(&context, [&continued](int) { continued = true; });
    QTest::qWait(20);
    QVERIFY(!ran);
    QVERIFY(!continued);
}

// A read that is still running when the reader shuts down (interrupted, or
// finishing on its own) never delivers what it had: its future is cancelled.
void DatabaseReaderTest::aReadUnderWayAtShutdownEndsCancelled()
{
    TempLibrary library;
    QVERIFY(library.ok());
    DatabaseReader reader(std::make_unique<ThreadDatabaseReadRunner>(library.database().paths()));
    std::atomic<bool> started = false;
    std::atomic<bool> release = false;
    QFuture<int> read = reader.read<int>([&started, &release](Database&) {
        started = true;
        while (!release.load()) {
            QThread::msleep(1);
        }
        return 42;
    });
    QTRY_VERIFY(started.load());
    // Shutdown waits for the job; let it finish once shutdown has begun.
    std::thread releaser([&release]() {
        QThread::msleep(20);
        release = true;
    });
    reader.shutdown();
    releaser.join();
    QVERIFY(read.isFinished());
    QVERIFY(read.isCanceled());
    QCOMPARE(read.resultCount(), 0);
}

QTEST_GUILESS_MAIN(DatabaseReaderTest)
#include "tst_database_reader.moc"
