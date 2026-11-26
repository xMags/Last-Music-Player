#pragma once

#include "core/account/AccountDataLifecycleService.h"
#include "core/account/AccountMusicContract.h"
#include "core/account/AccountSessionState.h"
#include "core/async/Result.h"
#include "core/db/AccountOutboxRecords.h"
#include "core/library/UserDataOperationGate.h"
#include "core/mode/ModeService.h"

#include <QFuture>
#include <QList>
#include <QJsonValue>
#include <QObject>
#include <QPromise>
#include <QString>
#include <QStringList>

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace lmp::core {
class PlaybackClock;
class PlaybackTimer;
class SettingsStore;
namespace db {
class AccountOutboxRepository;
class AccountRepository;
} // namespace db
} // namespace lmp::core

namespace lmp::core::account {

class AccountSessionService;
class AccountSyncTransport;

// Keeps the account library on this PC in step with the service, as earlier
// versions did. Five lanes run in this order: history (queued plays go up in
// batches of 100; a batch the service refuses is split until the refused
// play is found and blocked), likes (queued likes and unlikes, one by one),
// preference push (changed portable preferences), preference pull (the
// service's values come down; a key the service lacks is sent up once from
// the local setting, a pending local change wins), and library (the service
// library replaces the cached copy, with changes not yet sent laid over).
//
// A request adds lanes to the running sync, or starts one; every caller gets
// the result of the sync that ran its lanes. A sync needs the user data gate
// and a Validated session; every write checks that the session is still the
// one the sync started with, so results for a previous account are dropped.
// Passing failures (no connection, 408, 429, 5xx) are retried after
// accountRetryDelay, per row for the queues and per lane for the pulls; other
// refusals block the row for good (until a reset). A 401 hands the session to
// AccountSessionService::handleUnauthorized and ends the sync.
//
// Threading: GUI thread only; continuations run there with this object as
// their context.
class MusicSyncService final : public QObject, public AccountOutboundSync {
    Q_OBJECT

public:
    enum class Lane : std::uint32_t {
        None = 0,
        History = 1U << 0,
        Likes = 1U << 1,
        PreferencePush = 1U << 2,
        PreferencePull = 1U << 3,
        Library = 1U << 4,
        All = (1U << 5) - 1U,
    };

    // A change made here waits this long for others before it is sent.
    static constexpr std::chrono::milliseconds MutationDelay{1000};

    struct Dependencies {
        AccountSyncTransport& transport;
        AccountSessionService& session;
        mode::ModeService& mode;
        db::AccountRepository& accounts;
        db::AccountOutboxRepository& outbox;
        SettingsStore& settings;
        UserDataOperationGate& gate;
        PlaybackClock& clock;
        // ISO 8601 UTC now; the system clock when empty.
        std::function<QString()> nowUtc;
    };

    // Every dependency must outlive this object.
    explicit MusicSyncService(Dependencies dependencies, QObject* parent = nullptr);
    ~MusicSyncService() override;

    MusicSyncService(const MusicSyncService&) = delete;
    MusicSyncService& operator=(const MusicSyncService&) = delete;

    // Every lane, after making every queued row due again (a full sync, as
    // after signing in).
    [[nodiscard]] QFuture<bool> sync();
    // Runs `lanes` (added to a running sync). True when every lane succeeded.
    [[nodiscard]] QFuture<bool> requestSync(Lane lanes);
    // Sends what is queued here now (history, likes, preferences), retrying
    // rows that are not blocked at once. False when the schedule could not be
    // reset or a lane failed. Used before signing out.
    [[nodiscard]] QFuture<bool> requestOutboundSync() override;
    // A change was queued here: the lane runs after MutationDelay.
    void scheduleMutation(Lane lane);
    // Queues a portable preference changed here and schedules the push.
    // False when the value is not valid, no session is signed in (or
    // offline), or the gate is closed.
    bool savePortablePreference(PortablePreferenceKey key, const QString& valueJson);

    [[nodiscard]] bool isSyncing() const { return m_syncing; }
    // The last problem in words a user can read; empty after a clean sync.
    [[nodiscard]] QString lastSafeError() const { return m_lastSafeError; }
    [[nodiscard]] db::AccountPendingCounts pendingCounts() const;
    // Raised each time the library lane stored a new copy.
    [[nodiscard]] quint64 libraryRevision() const { return m_libraryRevision; }
    // How many songs `ownerId`'s synced history held at the last library
    // pull of this run; 0 before one, or for another owner.
    [[nodiscard]] int historyCount(const QString& ownerId) const
    {
        return ownerId == m_homeLayoutOwner ? m_historyCount : 0;
    }
    // Home's layout from the last library the service sent for `ownerId`
    // (Undefined for another owner or before the first library sync).
    [[nodiscard]] QJsonValue homeLayout(const QString& ownerId) const
    {
        return ownerId == m_homeLayoutOwner ? m_homeLayout : QJsonValue(QJsonValue::Undefined);
    }
    [[nodiscard]] quint64 preferenceRevision() const { return m_preferenceRevision; }

signals:
    void syncingChanged();
    void lastSafeErrorChanged();
    // The cached account library was replaced.
    void libraryChanged();
    // Preferences from the service were applied to the settings.
    void preferencesChanged();

private:
    enum class Outcome { Succeeded, Failed, Unauthorized, Cancelled };
    static constexpr std::size_t LaneCount = 5;

    [[nodiscard]] bool isCurrent() const;
    void runNextBatch();
    void runLaneFrom(std::size_t index);
    void finish(bool succeeded);
    [[nodiscard]] QFuture<Outcome> runLane(Lane lane);

    [[nodiscard]] QFuture<Outcome> historyStep(bool failed, int round);
    [[nodiscard]] QFuture<Outcome> uploadHistory(QList<db::PlaybackEventRecord> events);
    [[nodiscard]] QFuture<Outcome> uploadHalves(QList<db::PlaybackEventRecord> events);
    [[nodiscard]] QFuture<Outcome> likesStep(bool failed, int round);
    [[nodiscard]] QFuture<Outcome> sendLikes(QList<db::PendingLikeRecord> likes, qsizetype index,
                                             std::shared_ptr<bool> failed);
    [[nodiscard]] QFuture<Outcome> pushPreferences();
    [[nodiscard]] QFuture<Outcome> pullPreferences();
    [[nodiscard]] QFuture<Outcome> pullLibrary();

    void setSafeError(const QString& message);
    void saveSyncError(const QString& code);
    void scheduleRetry(Lane lane, std::chrono::seconds delay);
    void scheduleNextDurableRetry(Lane lane, db::AccountRetryQueue queue);
    int recordLaneFailure(Lane lane);
    void resetLaneFailure(Lane lane);
    void onSettingChanged(const QString& key);

    Dependencies m_deps;
    bool m_syncing = false;
    std::uint32_t m_pendingLanes = 0;
    std::uint32_t m_debouncedLanes = 0;
    std::uint32_t m_batchLanes = 0;
    bool m_allSucceeded = true;
    std::optional<AccountOperationContext> m_context;
    // The music source the sync started under; any change since (even away
    // and back) makes its results stale.
    std::optional<mode::RemoteScope> m_modeScope;
    std::optional<UserDataOperationGate::Lease> m_lease;
    std::vector<std::shared_ptr<QPromise<bool>>> m_waiters;
    quint64 m_libraryRevision = 0;
    // Numbers each library pull; only the newest one's answer is applied (its
    // parse runs off this thread, so answers could come back out of order).
    quint64 m_libraryPull = 0;
    // Kept with the home layout, for the same owner.
    int m_historyCount = 0;
    QJsonValue m_homeLayout = QJsonValue(QJsonValue::Undefined);
    QString m_homeLayoutOwner;
    quint64 m_preferenceRevision = 0;
    QString m_lastSafeError;
    // Set while preferences from the service are written to the settings, so
    // they are not sent back up as local changes.
    bool m_applyingRemote = false;
    std::unique_ptr<PlaybackTimer> m_mutationTimer;
    std::array<std::unique_ptr<PlaybackTimer>, LaneCount> m_retryTimers;
    std::array<std::uint64_t, LaneCount> m_retryDue{};
    std::array<int, LaneCount> m_laneFailures{};
};

[[nodiscard]] constexpr MusicSyncService::Lane operator|(MusicSyncService::Lane left,
                                                         MusicSyncService::Lane right) noexcept
{
    return static_cast<MusicSyncService::Lane>(static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

} // namespace lmp::core::account
