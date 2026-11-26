#include "core/db/DatabaseReader.h"

#include "core/db/Database.h"

#include <QMutexLocker>

#include <sqlite3.h>

namespace lmp::core::db {

ThreadDatabaseReadRunner::ThreadDatabaseReadRunner(AppPaths paths)
    : m_paths(std::move(paths))
{
    // One thread that is never retired, so the connection stays on it.
    m_pool.setMaxThreadCount(1);
    m_pool.setExpiryTimeout(-1);
}

ThreadDatabaseReadRunner::~ThreadDatabaseReadRunner()
{
    stop();
}

void ThreadDatabaseReadRunner::post(Job job)
{
    if (m_stopped.load()) {
        job(nullptr);
        return;
    }
    m_pool.start([this, job = std::move(job)]() { runJob(job); });
}

void ThreadDatabaseReadRunner::runJob(const Job& job)
{
    if (m_stopped.load()) {
        job(nullptr);
        return;
    }
    if (!m_database) {
        auto database = std::make_unique<Database>(m_paths);
        if (!database->open()) {
            job(nullptr);
            return;
        }
        // A reader: any write on this connection fails.
        static_cast<void>(database->connection().exec("PRAGMA query_only=ON;"));
        {
            const QMutexLocker locker(&m_handleLock);
            m_handle = database->connection().handle();
        }
        m_database = std::move(database);
    }
    job(m_database.get());
}

void ThreadDatabaseReadRunner::closeConnection()
{
    {
        const QMutexLocker locker(&m_handleLock);
        m_handle = nullptr;
    }
    m_database.reset();
}

void ThreadDatabaseReadRunner::stop()
{
    if (m_stopped.exchange(true)) {
        m_pool.waitForDone();
        return;
    }
    m_pool.clear();
    {
        // Safe from another thread while the connection is open; the lock
        // keeps it from closing meanwhile.
        const QMutexLocker locker(&m_handleLock);
        if (m_handle != nullptr) {
            sqlite3_interrupt(m_handle);
        }
    }
    // The connection is closed on its own thread, after the job under way.
    m_pool.start([this]() { closeConnection(); });
    m_pool.waitForDone();
}

DatabaseReader::DatabaseReader(std::unique_ptr<DatabaseReadRunner> runner)
    : m_runner(std::move(runner))
{
}

DatabaseReader::~DatabaseReader()
{
    shutdown();
}

void DatabaseReader::shutdown()
{
    if (m_shutDown) {
        return;
    }
    m_shutDown = true;
    m_stopped->store(true);
    m_runner->stop();
}

} // namespace lmp::core::db
