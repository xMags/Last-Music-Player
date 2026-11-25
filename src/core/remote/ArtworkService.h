#pragma once

#include "core/async/Result.h"
#include "core/mode/ModeService.h"

#include <QByteArray>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QObject>
#include <QPromise>
#include <QString>

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

namespace lmp::core {
class PlaybackClock;
class PlaybackTimer;
namespace account {
class AccountClient;
class AccountSessionService;
} // namespace account
namespace net {
class IHttpTransport;
}
} // namespace lmp::core

namespace lmp::core::remote {

class ArtworkDiskCache;

// Fetches cover pictures from the network, with the rules earlier versions
// used for account artwork. In Account mode every picture goes through the
// service's artwork relay (which needs the session, so an image element
// cannot fetch it itself), and a song known only by its source address has
// its picture looked up through resolve first. In the other modes the
// address is fetched as it is.
//
// Scheduling: at most MaxActive fetches run at once, of which at most
// MaxActiveBackfill may be off-screen work, so a picture that comes into view
// always finds a slot. Visible requests start first; a waiting off-screen
// request can be promoted. A fetch that fails for a passing reason (timeout,
// no connection, 408, 429, 5xx) is tried again after 500 ms and 1.5 s; a run
// that still fails that way is queued again after 2, 5, 12, then 30 s, for at
// most MaxRuns runs. Any other failure ends the request. The bytes of
// finished fetches are kept in memory, up to MaxCachedBytes, after which the
// whole cache is dropped and starts over. A change of account or mode drops
// the cache and every request (their futures finish as Cancelled).
//
// With a disk cache, a picture is looked for on disk before it is fetched,
// and a fetched one is written there, in the folder of the account (or of
// the open web) it was fetched for; so pictures show again without the
// network. keep() holds a picture on disk for good while something needs it
// (a downloaded song's cover), until release().
//
// At launch the stored account session is checked with the service before
// it can be used. A picture asked for in Account mode during that check waits
// for it, then is fetched under the session it produced (or fails as any
// request without a session does).
//
// Threading: GUI thread only; futures finish there.
class ArtworkService final : public QObject {
    Q_OBJECT

public:
    enum class Priority { Visible, Backfill };

    static constexpr int MaxActive = 6;
    static constexpr int MaxActiveBackfill = 2;
    static constexpr int MaxRuns = 5;
    static constexpr std::size_t MaxCachedBytes = 64U * 1024U * 1024U;
    static constexpr qint64 MaxPictureBytes = 12LL * 1024 * 1024;
    static constexpr std::array<std::chrono::milliseconds, 2> RetryDelays{std::chrono::milliseconds(500),
                                                                          std::chrono::milliseconds(1500)};
    // A server that asks for a longer wait than this (Retry-After) gets no
    // further try; the picture fails.
    static constexpr std::chrono::milliseconds MaxServerWait{std::chrono::minutes(5)};
    static constexpr std::array<std::chrono::milliseconds, 4> RunBackoff{
        std::chrono::milliseconds(2000), std::chrono::milliseconds(5000), std::chrono::milliseconds(12000),
        std::chrono::milliseconds(30000)};

    struct Dependencies {
        account::AccountClient& client;
        account::AccountSessionService& session;
        mode::ModeService& mode;
        // Fetches pictures outside Account mode.
        net::IHttpTransport& http;
        PlaybackClock& clock;
        // Pictures on disk between runs; may be missing (memory only then).
        ArtworkDiskCache* disk = nullptr;
    };

    // Every dependency must outlive this object.
    explicit ArtworkService(const Dependencies& dependencies, QObject* parent = nullptr);
    ~ArtworkService() override;

    ArtworkService(const ArtworkService&) = delete;
    ArtworkService& operator=(const ArtworkService&) = delete;

    // The picture's bytes. `artworkUrl` is the picture's address; when it is
    // empty, `sourceUrl` (a song's source address) is resolved for one, in
    // Account mode only. Fails with NotFound when neither is an http(s)
    // address. Requests for the same picture share one fetch.
    [[nodiscard]] QFuture<Result<QByteArray>> fetch(const QString& artworkUrl, const QString& sourceUrl = {},
                                                    Priority priority = Priority::Visible);
    // Moves a waiting off-screen request for that picture to the visible
    // queue (one that is waiting out a back-off keeps waiting).
    void promote(const QString& artworkUrl, const QString& sourceUrl = {});

    // Drops the cache and every request.
    void clear();

    // Keeps the picture on disk for good, from the disk cache when it is
    // there, else fetched (off-screen work). True once it is kept; false
    // without a disk cache, an account to keep it for, or a picture.
    [[nodiscard]] QFuture<bool> keep(const QString& artworkUrl, const QString& sourceUrl = {});
    // A kept picture nothing needs any more goes back to the disk cache.
    void release(const QString& artworkUrl, const QString& sourceUrl = {});
    // Releases every kept picture of the current account except these:
    // what was kept for things removed while the app was closed.
    void keepOnly(const QList<QString>& artworkUrls);
    // The disk file key of the picture now; empty without a disk cache, an
    // account, or a picture address.
    [[nodiscard]] QString diskKey(const QString& artworkUrl, const QString& sourceUrl = {}) const;
    // Whether pictures can be kept on disk now (a disk cache, and an
    // account to keep them for in Account mode).
    [[nodiscard]] bool diskReady() const { return !diskScope().isEmpty(); }

    [[nodiscard]] int activeCount() const { return m_active; }
    [[nodiscard]] std::size_t cachedBytes() const { return m_cacheBytes; }
    [[nodiscard]] bool isCached(const QString& artworkUrl, const QString& sourceUrl = {}) const;

signals:
    // The account or mode changed: pictures kept for the old one are not
    // this one's (clear() ran).
    void scopeChanged();

private:
    using Promise = std::shared_ptr<QPromise<Result<QByteArray>>>;

    // A fetch asked for while the stored account session was still being
    // checked; it is asked for again once that check ends.
    struct AwaitingSession {
        QString artworkUrl;
        QString sourceUrl;
        Priority priority = Priority::Visible;
        Promise promise;
    };

    struct Request {
        quint64 id = 0;
        QString artworkUrl;
        QString sourceUrl;
        Priority priority = Priority::Visible;
        bool deferred = false;
        // Being looked for on disk; not queued for the network yet.
        bool probing = false;
        // Where it is written on disk once fetched; empty: not written.
        QString diskKey;
        int runs = 0;
        std::vector<Promise> waiters;
    };

    // A step that waits for a time: the next attempt of a run, or a deferred
    // request going back to its queue.
    struct DueStep {
        enum class Kind { NextAttempt, Requeue };
        Kind kind = Kind::Requeue;
        QString key;
        quint64 id = 0;
        std::uint64_t at = 0;
        // NextAttempt only.
        int attempt = 0;
        Priority slot = Priority::Visible;
        QString url;
        mode::RemoteScope scope;
    };

    [[nodiscard]] static QString keyFor(const QString& artworkUrl, const QString& sourceUrl);
    // The request key fetch() would use, or empty when it would not fetch.
    [[nodiscard]] QString requestKey(const QString& artworkUrl, const QString& sourceUrl) const;
    // Whose pictures the disk holds now: the account, or the open web.
    [[nodiscard]] QString diskScope() const;
    void onProbe(const QString& key, quint64 id, const std::optional<QByteArray>& bytes);
    void startRequests();
    void startRun(const QString& key, Priority slot);
    void attempt(const QString& key, quint64 id, Priority slot, int attemptIndex, const QString& url,
                 const mode::RemoteScope& scope);
    void onAttempt(const QString& key, quint64 id, Priority slot, int attemptIndex, const QString& url,
                   const mode::RemoteScope& scope, const Result<QByteArray>& bytes);
    void finishFailure(const QString& key, Priority slot, const mode::RemoteScope& scope, const Failure& failure,
                       bool transient);
    void complete(Priority slot);
    void schedule(DueStep step);
    void runDueSteps();
    void armTimer();
    [[nodiscard]] QFuture<Result<QString>> artworkFor(const QString& artworkUrl, const QString& sourceUrl);
    [[nodiscard]] QFuture<Result<QByteArray>> download(const QString& url);
    void resumeAwaitingSession();
    void store(const QString& key, const QByteArray& bytes);
    static void settle(std::vector<Promise>& waiters, const Result<QByteArray>& result);

    Dependencies m_deps;
    QHash<QString, Request> m_requests;
    QHash<QString, QByteArray> m_cache;
    std::size_t m_cacheBytes = 0;
    std::deque<QString> m_visibleQueue;
    std::deque<QString> m_backfillQueue;
    std::vector<DueStep> m_due;
    // Not dropped by clear(): the owner change at the end of the check is
    // what they wait for.
    std::vector<AwaitingSession> m_awaitingSession;
    std::unique_ptr<PlaybackTimer> m_timer;
    quint64 m_nextId = 0;
    int m_active = 0;
    int m_activeBackfill = 0;
    // The mode the cache was filled under; a different one drops it.
    mode::SourceMode m_lastMode = mode::SourceMode::LocalOnly;
};

} // namespace lmp::core::remote
