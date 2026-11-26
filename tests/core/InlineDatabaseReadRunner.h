#pragma once

#include <vector>

#include "core/db/Database.h"
#include "core/db/DatabaseReader.h"

namespace lmp::test {

// Runs a reader's jobs on the test's thread against the test's own database:
// at once, or, while `holding`, when the test calls release(). Answers still
// reach `.then(context, ...)` continuations on the next event-loop turn, as
// they do from the worker.
class InlineDatabaseReadRunner final : public core::db::DatabaseReadRunner {
public:
    explicit InlineDatabaseReadRunner(core::db::Database& database)
        : m_database(database)
    {
    }

    void post(Job job) override
    {
        ++posted;
        if (stopped) {
            job(nullptr);
            return;
        }
        if (holding) {
            held.push_back(std::move(job));
            return;
        }
        job(&m_database);
    }

    void stop() override
    {
        stopped = true;
        for (Job& job : held) {
            job(nullptr);
        }
        held.clear();
    }

    // Runs the held jobs in order (ones they post are held again while
    // `holding`).
    void release()
    {
        std::vector<Job> jobs;
        jobs.swap(held);
        for (Job& job : jobs) {
            job(&m_database);
        }
    }

    bool holding = false;
    bool stopped = false;
    int posted = 0;
    std::vector<Job> held;

private:
    core::db::Database& m_database;
};

} // namespace lmp::test
