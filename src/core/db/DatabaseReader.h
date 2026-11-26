#pragma once

#include <QFuture>
#include <QMutex>
#include <QPromise>
#include <QThreadPool>

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

#include "core/app/AppPaths.h"

struct sqlite3;

namespace lmp::core::db {

class Database;

// Where a reader's jobs run. A job gets the reading database, or nullptr when
// it cannot run (the runner stopped, or its connection could not open).
class DatabaseReadRunner {
public:
    using Job = std::function<void(Database*)>;

    virtual ~DatabaseReadRunner() = default;
    DatabaseReadRunner(const DatabaseReadRunner&) = delete;
    DatabaseReadRunner& operator=(const DatabaseReadRunner&) = delete;

    // Runs `job`, after every job posted before it.
    virtual void post(Job job) = 0;
    // Runs no more jobs: queued ones are dropped, one under way is
    // interrupted and waited for. Safe to call twice.
    virtual void stop() = 0;

protected:
    DatabaseReadRunner() = default;
};

// The application's runner: one worker thread, kept for the runner's life,
// with its own connection to the library database. The connection is opened
// on that thread at the first job (the usual pragmas, then query_only, so it
// can never write), used only there, and closed there by stop(). The main
// connection keeps the database in WAL mode, so these reads run beside its
// writes and see what was committed when each read began.
class ThreadDatabaseReadRunner final : public DatabaseReadRunner {
public:
    // The main connection has created the schema before the first job.
    explicit ThreadDatabaseReadRunner(AppPaths paths);
    ~ThreadDatabaseReadRunner() override;

    void post(Job job) override;
    void stop() override;

private:
    // On the worker thread.
    void runJob(const Job& job);
    void closeConnection();

    AppPaths m_paths;
    QThreadPool m_pool;
    // Created, used and destroyed on the worker thread only.
    std::unique_ptr<Database> m_database;
    // The open connection, for stop() to interrupt from another thread.
    QMutex m_handleLock;
    sqlite3* m_handle = nullptr;
    std::atomic<bool> m_stopped = false;
};

// Reads the library database off the GUI thread. read() runs `work` on the
// runner, in order, and hands its answer back as a future: continue it with
// `.then(context, ...)` to receive it on the context's thread, never after
// the context is gone. A read that cannot run (after shutdown) has a
// cancelled future, so its continuation does not run at all.
//
// The owner (the composition root) calls shutdown() before the main database
// closes. GUI thread only; the work itself runs on the runner's thread and
// must touch nothing but the database it is given and what it captured.
class DatabaseReader {
public:
    explicit DatabaseReader(std::unique_ptr<DatabaseReadRunner> runner);
    ~DatabaseReader();
    DatabaseReader(const DatabaseReader&) = delete;
    DatabaseReader& operator=(const DatabaseReader&) = delete;

    template <typename T>
    [[nodiscard]] QFuture<T> read(std::function<T(Database&)> work)
    {
        auto promise = std::make_shared<QPromise<T>>();
        promise->start();
        QFuture<T> future = promise->future();
        if (m_shutDown) {
            future.cancel();
            promise->finish();
            return future;
        }
        // A dropped job destroys its promise unfinished, which cancels it.
        // A read that shutdown() interrupted returns what it had read so far:
        // it ends cancelled too, never with a partial answer.
        m_runner->post([promise, stopped = m_stopped, work = std::move(work)](Database* database) {
            if (database == nullptr || stopped->load()) {
                promise->future().cancel();
                promise->finish();
                return;
            }
            T answer = work(*database);
            if (stopped->load()) {
                promise->future().cancel();
            } else {
                promise->addResult(std::move(answer));
            }
            promise->finish();
        });
        return future;
    }

    // Stops reading: later reads are cancelled, one under way is interrupted
    // and waited for. Safe to call twice.
    void shutdown();

private:
    std::unique_ptr<DatabaseReadRunner> m_runner;
    bool m_shutDown = false;
    // Set before the runner is stopped (and a read under way interrupted);
    // shared with the jobs, which run on the runner's thread.
    std::shared_ptr<std::atomic<bool>> m_stopped = std::make_shared<std::atomic<bool>>(false);
};

} // namespace lmp::core::db
