#pragma once

#include "core/async/Result.h"
#include "core/db/AccountOutboxRecords.h"
#include "core/db/AccountRecords.h"
#include "core/db/LibraryQueryTypes.h"

#include <QByteArray>
#include <QFuture>
#include <QList>
#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>

namespace lmp::core {
class UserDataOperationGate;
} // namespace lmp::core

namespace lmp::core::account {

// The earlier versions' one-time offer, in Account mode, to import the
// listening history of remote songs played on this PC into the account.
// Asked once per account after a sign-in sync; the answer is kept in the
// account's sync state ("declined", "accepted", "completed"). An accepted
// import that stopped (network, quit) carries on after the next sync
// without asking again. Only songs the account service resolves are sent;
// local files stay on this PC.
//
// Each song is resolved, then queued as a play event with a stable id (so
// a retry never adds it twice); the history lane sends them.
//
// GUI thread.
class HistoryImportService final : public QObject {
    Q_OBJECT

public:
    struct Dependencies {
        UserDataOperationGate& gate;
        // The signed-in owner while the importLocalHistory capability holds
        // and the session is validated; empty otherwise.
        std::function<QString()> accountId;
        std::function<db::AccountSyncState(const QString& accountId)> loadState;
        std::function<bool(const db::AccountSyncState& state)> saveState;
        std::function<QList<db::HistoryImportCandidate>()> candidates;
        // The account service's resolve of a source address.
        std::function<QFuture<Result<QByteArray>>(const QString& sourceUrl)> resolve;
        std::function<bool(const db::PlaybackEventRecord& event)> enqueue;
        // Sends the queued history.
        std::function<void()> sendHistory;
    };

    // Every dependency must outlive this object.
    explicit HistoryImportService(Dependencies dependencies, QObject* parent = nullptr);

    [[nodiscard]] bool offering() const noexcept { return m_offering; }
    [[nodiscard]] bool importing() const noexcept { return m_importing; }

    // After a sync: asks (offerChanged), resumes an accepted import, or does
    // nothing.
    void offerIfDue();
    void accept();
    void decline();

signals:
    void offerChanged();
    void importingChanged();
    // The import ran to the end.
    void finished(int imported, int skipped);
    // The import stopped and carries on after the next sync.
    void paused(const QString& message);

private:
    void start();
    void step();
    void handleResolved(std::uint64_t run, const db::HistoryImportCandidate& candidate, const QString& eventId,
                        const QString& playedAt, const Result<QByteArray>& answer);
    void complete();
    void pause();
    void stop();
    [[nodiscard]] bool stillCurrent() const;
    [[nodiscard]] bool saveDecision(const QString& value);

    Dependencies m_deps;
    bool m_offering = false;
    bool m_importing = false;
    QString m_owner;
    std::uint64_t m_gateGeneration = 0;
    std::uint64_t m_run = 0;
    QList<db::HistoryImportCandidate> m_queue;
    qsizetype m_next = 0;
    int m_imported = 0;
    int m_skipped = 0;
};

} // namespace lmp::core::account
